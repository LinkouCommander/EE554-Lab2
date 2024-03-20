#ifndef __LEARNING_GEM5_SIMPLE_CACHE_SIMPLE_CACHE_HH__
#define __LEARNING_GEM5_SIMPLE_CACHE_SIMPLE_CACHE_HH__

#include <unordered_map>

#include "base/statistics.hh"
#include "mem/port.hh"
#include "params/SimpleCache.hh"
#include "sim/clocked_object.hh"

namespace gem5
{

class SimpleCache : public ClockedObject
{
  private:
    // slave
    class CPUSidePort : public SlavePort
    {
      private:
      	int id;
        SimpleCache *owner;
        bool needRetry;
        PacketPtr blockedPacket;

      public:
        CPUSidePort(const std::string& name, int id, SimpleCache *owner) :
            SlavePort(name, owner), id(id), owner(owner), needRetry(false), blockedPacket(nullptr)
        { }
        
        // Send a packet across this port. This is called by the owner and all of the flow control is hanled in this function.
        void sendPacket(PacketPtr pkt);

        // Send a retry to the peer port only if it is needed. This is called from the SimpleCache whenever it is unblocked.
        void trySendRetry();

        // Get a list of the non-overlapping address ranges the owner is responsible for. 
        // All response ports must override this function and return a populated list with at least one item.
        AddrRangeList getAddrRanges() const override;

      protected:
        Tick recvAtomic(PacketPtr pkt) override { panic("recvAtomic unimpl."); }
        void recvFunctional(PacketPtr pkt) override;
        bool recvTimingReq(PacketPtr pkt) override;
        void recvRespRetry() override;
    };

    // master
    class MemSidePort : public MasterPort
    {
      private:
        SimpleCache *owner;
        PacketPtr blockedPacket;

      public:
        MemSidePort(const std::string& name, SimpleCache *owner) :
            MasterPort(name, owner), owner(owner), blockedPacket(nullptr)
        { }
        
        // Send a packet across this port. This is called by the owner and all of the flow control is hanled in this function.
        void sendPacket(PacketPtr pkt);

      protected:
        bool recvTimingResp(PacketPtr pkt) override;
        void recvReqRetry() override;
        void recvRangeChange() override;
    };
    
    // Handle the request from the CPU side
    // @return true if we can handle the request this cycle, 
    //         false if the requestor needs to retry later
    bool handleRequest(PacketPtr pkt, int port_id);

    // Handle the respone from the memory side
    // @return true if we can handle the response this cycle, 
    //         false if the responder needs to retry later
    bool handleResponse(PacketPtr pkt);

    // Send the packet to the CPU side.
    void sendResponse(PacketPtr pkt);

    // Handle a packet functionally. Update the data on a write and get the data on a read.
    void handleFunctional(PacketPtr pkt);

    // Access the cache for a timing access. This is called after the cache access latency has already elapsed.
    void accessTiming(PacketPtr pkt);
    
    // This is where we actually update / read from the cache. This function is executed on both timing and functional accesses.
    // @return true if a hit, false otherwise
    bool accessFunctional(PacketPtr pkt);

    // Insert a block into the cache. If there is no room left in the cache, then this function evicts a random entry t make room for the new block.
    void insert(PacketPtr pkt);    
    
    // Return the address ranges this memobj is responsible for. Just use the same as the next upper level of the hierarchy.
    AddrRangeList getAddrRanges() const;

    // Tell the CPU side to ask for our memory ranges
    void sendRangeChange() const;
    
    // Latency to check the cache
    const Cycles latency;

    // The block size for the cache
    const unsigned blockSize;

    // Number of blocks in the cache
    const unsigned capacity;

    // Instantiation of the CPU-side port
    std::vector<CPUSidePort> cpuPorts;

    // Instantiation of the memory-side port
    MemSidePort memPort;

    // True if this cache is currently blocked waiting for a response.
    bool blocked;

    // Packet that we are currently handling. Used for upgrading to larger cache line sizes
    PacketPtr outstandingPacket;

    // The port to send the response when we recieve it back
    int waitingPortId;

    // track the miss latency
    Tick missTime;

    // cache storage. Maps block addresses to data
    std::unordered_map<Addr, uint8_t*> cacheStore;

    /// Cache statistics
  protected:
    struct SimpleCacheStats : public statistics::Group
    {
        SimpleCacheStats(statistics::Group *parent);
        statistics::Scalar hits;
        statistics::Scalar misses;
        statistics::Histogram missLatency;
        statistics::Formula hitRatio;
    } stats;
    
  public:

    /** constructor
     */
    SimpleCache(const SimpleCacheParams &params);
    
    Port &getPort(const std::string &if_name,
                  PortID idx=InvalidPortID) override;
};

} // namespace gem5

#endif // __LEARNING_GEM5_PART2_SIMPLE_MEMOBJ_HH__
