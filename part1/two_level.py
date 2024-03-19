import m5
from m5.objects import *
from caches import *

m5.util.addToPath("../../")

from common import SimpleOpts

default_binary = 'tests/test-progs/hello/bin/x86/linux/hello'
SimpleOpts.add_option("binary", nargs="?", default=default_binary)
args = SimpleOpts.parse_args()

system = System()

# clock
system.clk_domain = SrcClockDomain()
system.clk_domain.clock = '1GHz'
system.clk_domain.voltage_domain = VoltageDomain()

# memory
system.mem_mode = 'timing' # 'timing mode' considers timing, ordering of memory accesses (simulate real world memory which exists latency)
                           # 'functional mode' ignores latency of memory accesses
system.mem_ranges = [AddrRange('512MB')]

# CPU
system.cpu = X86TimingSimpleCPU() # generate a x86 CPU

# L1Caches
system.cpu.icache = L1ICache(args)
system.cpu.dcache = L1DCache(args)

system.cpu.icache.connectCPU(system.cpu)
system.cpu.dcache.connectCPU(system.cpu)

# L1L2 bus
system.l2bus = L2XBar()

system.cpu.icache.connectBus(system.l2bus)
system.cpu.dcache.connectBus(system.l2bus)

system.l2cache = L2Cache(args)
system.l2cache.connectCPUSideBus(system.l2bus)

# memory bus
system.membus = SystemXBar()
system.l2cache.connectMemSideBus(system.membus)


# Connecting the PIO and interrupt ports to the memory bus is an x86-specific requirement. Other ISAs (e.g., ARM) do not require these 3 extra lines.
system.cpu.createInterruptController()
system.cpu.interrupts[0].pio = system.membus.mem_side_ports
system.cpu.interrupts[0].int_requestor = system.membus.cpu_side_ports
system.cpu.interrupts[0].int_responder = system.membus.mem_side_ports

system.system_port = system.membus.cpu_side_ports

# memory controller
system.mem_ctrl = MemCtrl()
system.mem_ctrl.dram = DDR3_1600_8x8()
system.mem_ctrl.dram.range = system.mem_ranges[0]
system.mem_ctrl.port = system.membus.mem_side_ports

# create a process to run 'hello world'
system.workload = SEWorkload.init_compatible(args.binary) # use SE(Syscall emulation) mode which focus on simulating the CPU and memory system
                                                     # FS(Full system) mode runs an unmodified kernel
process = Process()
process.cmd = [args.binary]
system.cpu.workload = process
system.cpu.createThreads()

root = Root(full_system = False, system = system)
m5.instantiate()

print("Beginning simulation!")
exit_event = m5.simulate()

print('Exiting @ tick {} because {}'
      .format(m5.curTick(), exit_event.getCause()))