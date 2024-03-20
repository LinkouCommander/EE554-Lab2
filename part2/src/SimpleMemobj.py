from m5.params import *
from m5.proxy import *
from m5.SimObject import SimObject

class SimpleMemobj(SimObject):
    type = 'SimpleMemobj'
    cxx_header = "learning_gem5/part2/simple_memobj.hh"
    cxx_class = 'gem5::SimpleMemobj'

    inst_port = SlavePort("CPU side port, receives requests")
    data_port = SlavePort("CPU side port, receives requests")
    mem_side = MasterPort("Memory side port, sends requests")
