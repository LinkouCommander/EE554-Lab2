#include "learning_gem5/part2/simple_cache.hh"

#include "base/compiler.hh"
#include "base/random.hh"
#include "debug/SimpleCache.hh"
#include "sim/system.hh"

namespace gem5
{

SimpleCache::SimpleCache(const SimpleCacheParams &params) :
    ClockedObject(params),
    latency(params.latency),
    blockSize(params.system->cacheLineSize()),
    capacity(params.size / blockSize),
    memPort(params.name + ".mem_side", this),
    blocked(false), 
    outstandingPacket(nullptr), 
    waitingPortId(-1),
    stats(this)
{
    for (int i = 0; i < params.port_cpu_side_connection_count; ++i) {
        cpuPorts.emplace_back(name() + csprintf(".cpu_side[%d]", i), i, this);
    }
}

// implement the interfaces
Port &
SimpleCache::getPort(const std::string &if_name, PortID idx)
{
    // This is the name from the Python SimObject declaration (SimpleCache.py)
    if (if_name == "mem_side") {
        panic_if(idx != InvalidPortID, "Mem side of simple cache not a vector port");
        return memPort;
    } else if (if_name == "cpu_side" && idx < cpuPorts.size()) {
        return cpuPorts[idx];
    } else {
        // pass it along to our super class
        return ClockedObject::getPort(if_name, idx);
    }
}


// implement slave & master port functions
// CPU Side (slave)
AddrRangeList
SimpleCache::CPUSidePort::getAddrRanges() const
{
    return owner->getAddrRanges();
}

void
SimpleCache::CPUSidePort::recvFunctional(PacketPtr pkt)
{
    return owner->handleFunctional(pkt);
}

// recvTimingReq
bool
SimpleCache::CPUSidePort::recvTimingReq(PacketPtr pkt)
{
    DPRINTF(SimpleCache, "Got request %s\n", pkt->print());

    if (blockedPacket || needRetry) {
        // The cache may not be able to send a reply if this is blocked
        DPRINTF(SimpleCache, "Request blocked\n");
        needRetry = true;
        return false;
    }
    // Just forward to the cache.
    if (!owner->handleRequest(pkt, id)) {
        DPRINTF(SimpleCache, "Request failed\n");
        // stalling
        needRetry = true;
        return false;
    } else {
        DPRINTF(SimpleCache, "Request succeeded\n");
        return true;
    }
}

// store the packet if blocked
void
SimpleCache::CPUSidePort::sendPacket(PacketPtr pkt)
{
    panic_if(blockedPacket != nullptr, "Should never try to send if blocked!");

    if (!sendTimingResp(pkt)) {
        blockedPacket = pkt;
    }
}

// resend blocked packet
void
SimpleCache::CPUSidePort::recvRespRetry()
{
    assert(blockedPacket != nullptr);

    PacketPtr pkt = blockedPacket;
    blockedPacket = nullptr;

    sendPacket(pkt);
}

// check if need retry
void
SimpleCache::CPUSidePort::trySendRetry()
{
    if (needRetry && blockedPacket == nullptr) {
        needRetry = false;
        DPRINTF(SimpleCache, "Sending retry req for %d\n", id);
        sendRetryReq();
    }
}


// Memory Side (master)
void
SimpleCache::MemSidePort::recvRangeChange()
{
    owner->sendRangeChange();
}

// store the packet if blocked
void
SimpleCache::MemSidePort::sendPacket(PacketPtr pkt)
{
    panic_if(blockedPacket != nullptr, "Should never try to send if blocked!");
    if (!sendTimingReq(pkt)) {
        blockedPacket = pkt;
    }
}

// resend blocked packet
void
SimpleCache::MemSidePort::recvReqRetry()
{
    assert(blockedPacket != nullptr);

    PacketPtr pkt = blockedPacket;
    blockedPacket = nullptr;

    sendPacket(pkt);
}

// recvTimingResp
bool
SimpleCache::MemSidePort::recvTimingResp(PacketPtr pkt)
{
    return owner->handleResponse(pkt);
}

/*---------------------------------------------------------*/
void
SimpleCache::handleFunctional(PacketPtr pkt)
{
    if (accessFunctional(pkt)) {
        pkt->makeResponse();
    } else {
        memPort.sendFunctional(pkt);
    }
}

AddrRangeList
SimpleCache::getAddrRanges() const
{
    DPRINTF(SimpleCache, "Sending new ranges\n");
    return memPort.getAddrRanges();
}

bool
SimpleCache::handleRequest(PacketPtr pkt, int port_id)
{
    if (blocked) {
        return false;
    }
    DPRINTF(SimpleCache, "Got request for addr %#x\n", pkt->getAddr());
    
    blocked = true;
    
    // Store the port for when we get the response
    assert(waitingPortId == -1);
    waitingPortId = port_id;
    
    schedule(new EventFunctionWrapper([this, pkt]{ accessTiming(pkt); }, name() + ".accessEvent", true), clockEdge(latency));
    
    return true;
}

void
SimpleCache::sendResponse(PacketPtr pkt)
{
    assert(blocked);
    DPRINTF(SimpleCache, "Got response for addr %#x\n", pkt->getAddr());
    
    int port = waitingPortId;

    blocked = false;
    waitingPortId = -1;

    cpuPorts[port].sendPacket(pkt);
    for (auto& port : cpuPorts) {
        port.trySendRetry();
    }
}

// handle the cache miss case
void
SimpleCache::accessTiming(PacketPtr pkt)
{
    bool hit = accessFunctional(pkt);

    DPRINTF(SimpleCache, "%s for packet: %s\n", hit ? "Hit" : "Miss",
            pkt->print());

    if (hit) {
        // Respond to the CPU side
        stats.hits++; // update stats
        DDUMP(SimpleCache, pkt->getConstPtr<uint8_t>(), pkt->getSize());
        pkt->makeResponse();
        sendResponse(pkt);
    } else {
        stats.misses++; // update stats
        missTime = curTick();
        // Forward to the memory side.
        // We can't directly forward the packet unless it is exactly the size
        // of the cache line, and aligned. Check for that here.
        Addr addr = pkt->getAddr();
        Addr block_addr = pkt->getBlockAddr(blockSize);
        unsigned size = pkt->getSize();
        if (addr == block_addr && size == blockSize) {
            // Aligned and block size. We can just forward.
            DPRINTF(SimpleCache, "forwarding packet\n");
            memPort.sendPacket(pkt);
        } else {
            DPRINTF(SimpleCache, "Upgrading packet to block size\n");
            panic_if(addr - block_addr + size > blockSize,
                     "Cannot handle accesses that span multiple cache lines");
            // Unaligned access to one cache block
            assert(pkt->needsResponse());
            MemCmd cmd;
            if (pkt->isWrite() || pkt->isRead()) {
                // Read the data from memory to write into the block.
                // We'll write the data in the cache (i.e., a writeback cache)
                cmd = MemCmd::ReadReq;
            } else {
                panic("Unknown packet type in upgrade size");
            }

            // Create a new packet that is blockSize
            PacketPtr new_pkt = new Packet(pkt->req, cmd, blockSize);
            new_pkt->allocate();

            // Should now be block aligned
            assert(new_pkt->getAddr() == new_pkt->getBlockAddr(blockSize));

            // Save the old packet
            outstandingPacket = pkt;

            DPRINTF(SimpleCache, "forwarding packet\n");
            memPort.sendPacket(new_pkt);
        }
    }
}


bool
SimpleCache::handleResponse(PacketPtr pkt)
{
    assert(blocked);
    DPRINTF(SimpleCache, "Got response for addr %#x\n", pkt->getAddr());
    insert(pkt);
    
    stats.missLatency.sample(curTick() - missTime);

    // If we had to upgrade the request packet to a full cache line, now we
    // can use that packet to construct the response.
    if (outstandingPacket != nullptr) {
        DPRINTF(SimpleCache, "Copying data from new packet to old\n");
        // We had to upgrade a previous packet. We can functionally deal with
        // the cache access now. It better be a hit.
        [[maybe_unused]] bool hit = accessFunctional(outstandingPacket);
        panic_if(!hit, "Should always hit after inserting");
        outstandingPacket->makeResponse();
        delete pkt; // We may need to delay this, I'm not sure.
        pkt = outstandingPacket;
        outstandingPacket = nullptr;
    } // else, pkt contains the data it needs

    sendResponse(pkt);

    return true;
}

// check if the map has address in the packet
bool
SimpleCache::accessFunctional(PacketPtr pkt)
{
    Addr block_addr = pkt->getBlockAddr(blockSize);
    auto it = cacheStore.find(block_addr);
    if (it != cacheStore.end()) {
        if (pkt->isWrite()) {
            pkt->writeDataToBlock(it->second, blockSize);
        } else if (pkt->isRead()) {
            pkt->setDataFromBlock(it->second, blockSize);
        } else {
            panic("Unknown packet type!");
        }
        return true;
    }
    return false;
}

// This function is called every time the memory side port responds to a request
void
SimpleCache::insert(PacketPtr pkt)
{
    // The packet should be aligned.
    assert(pkt->getAddr() ==  pkt->getBlockAddr(blockSize));
    // The address should not be in the cache
    assert(cacheStore.find(pkt->getAddr()) == cacheStore.end());
    // The pkt should be a response
    assert(pkt->isResponse());
    
    if (cacheStore.size() >= capacity) {
        // Select random thing to evict.
        int bucket, bucket_size;
        do {
            bucket = random_mt.random(0, (int)cacheStore.bucket_count() - 1);
        } while ( (bucket_size = cacheStore.bucket_size(bucket)) == 0 );
        auto block = std::next(cacheStore.begin(bucket),
                               random_mt.random(0, bucket_size - 1));
                               
        DPRINTF(SimpleCache, "Removing addr %#x\n", block->first);

        RequestPtr req = std::make_shared<Request>(block->first, blockSize, 0, 0);
        PacketPtr new_pkt = new Packet(req, MemCmd::WritebackDirty, blockSize);
        new_pkt->dataDynamic(block->second); // This will be deleted later

        DPRINTF(SimpleCache, "Writing packet back %s\n", pkt->print());
        memPort.sendTimingReq(new_pkt);

        cacheStore.erase(block->first);
    }
    
    DPRINTF(SimpleCache, "Inserting %s\n", pkt->print());
    DDUMP(SimpleCache, pkt->getConstPtr<uint8_t>(), blockSize);    
    
    uint8_t *data = new uint8_t[blockSize];
    cacheStore[pkt->getAddr()] = data;

    pkt->writeDataToBlock(data, blockSize);
}

SimpleCache::SimpleCacheStats::SimpleCacheStats(statistics::Group *parent)
      : statistics::Group(parent),
      ADD_STAT(hits, statistics::units::Count::get(), "Number of hits"),
      ADD_STAT(misses, statistics::units::Count::get(), "Number of misses"),
      ADD_STAT(missLatency, statistics::units::Tick::get(),
               "Ticks for misses to the cache"),
      ADD_STAT(hitRatio, statistics::units::Ratio::get(),
               "The ratio of hits to the total accesses to the cache",
               hits / (hits + misses))
{
    missLatency.init(16); // number of buckets
}

void
SimpleCache::sendRangeChange() const
{
    for (auto& port : cpuPorts) {
        port.sendRangeChange();
    }
}

} // namespace gem5
