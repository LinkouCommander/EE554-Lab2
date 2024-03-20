from m5.params import *
from m5.proxy import *
from m5.objects.ClockedObject import ClockedObject

class SimpleCache(ClockedObject):
    type = 'SimpleCache'
    cxx_header = "learning_gem5/part2/simple_cache.hh"
    cxx_class = "gem5::SimpleCache"

    # Vector port example. Both the instruction and data ports connect to this
    # port which is automatically split out into two ports.
    cpu_side = VectorSlavePort("CPU side port, receives requests")
    mem_side = MasterPort("Memory side port, sends requests")

    latency = Param.Cycles(1, "Cycles taken on a hit or to resolve a miss")

    size = Param.MemorySize('16kB', "The size of the cache")

    system = Param.System(Parent.any, "The system this cache is part of")
