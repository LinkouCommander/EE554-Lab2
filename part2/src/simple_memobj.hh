#ifndef __LEARNING_GEM5_PART2_SIMPLE_MEMOBJ_HH__
#define __LEARNING_GEM5_PART2_SIMPLE_MEMOBJ_HH__

#include "mem/port.hh"
#include "params/SimpleMemobj.hh"
#include "sim/sim_object.hh"

namespace gem5
{

class SimpleMemobj : public SimObject
{
  private:
    // slave
    class CPUSidePort : public SlavePort
    {
      private:
        SimpleMemobj *owner;
        bool needRetry;
        PacketPtr blockedPacket;

      public:
        CPUSidePort(const std::string& name, SimpleMemobj *owner) :
            SlavePort(name, owner), owner(owner), needRetry(false), blockedPacket(nullptr)
        { }
        
        // Send a packet across this port. This is called by the owner and all of the flow control is hanled in this function.
        void sendPacket(PacketPtr pkt);

        // Send a retry to the peer port only if it is needed. This is called from the SimpleMemobj whenever it is unblocked.
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
        SimpleMemobj *owner;
        PacketPtr blockedPacket;

      public:
        MemSidePort(const std::string& name, SimpleMemobj *owner) :
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
    bool handleRequest(PacketPtr pkt);

    // Handle the respone from the memory side
    // @return true if we can handle the response this cycle, 
    //         false if the responder needs to retry later
    bool handleResponse(PacketPtr pkt);

    // Handle a packet functionally. Update the data on a write and get the data on a read.
    void handleFunctional(PacketPtr pkt);

    // Return the address ranges this memobj is responsible for. Just use the same as the next upper level of the hierarchy.
    AddrRangeList getAddrRanges() const;

    // Tell the CPU side to ask for our memory ranges
    void sendRangeChange();

    CPUSidePort instPort;
    CPUSidePort dataPort;

    MemSidePort memPort;
    
    bool blocked;
    
  public:

    /** constructor
     */
    SimpleMemobj(const SimpleMemobjParams &params);
    Port &getPort(const std::string &if_name,
                  PortID idx=InvalidPortID) override;
};

} // namespace gem5

#endif // __LEARNING_GEM5_PART2_SIMPLE_MEMOBJ_HH__
