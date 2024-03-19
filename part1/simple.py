import m5
from m5.objects import *

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

# memory bus
system.membus = SystemXBar()
system.cpu.icache_port = system.membus.cpu_side_ports
system.cpu.dcache_port = system.membus.cpu_side_ports

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
binary = 'tests/test-progs/hello/bin/x86/linux/hello'

system.workload = SEWorkload.init_compatible(binary) # use SE(Syscall emulation) mode which focus on simulating the CPU and memory system
                                                     # FS(Full system) mode runs an unmodified kernel
process = Process()
process.cmd = [binary]
system.cpu.workload = process
system.cpu.createThreads()

root = Root(full_system = False, system = system)
m5.instantiate()

print("Beginning simulation!")
exit_event = m5.simulate()

print('Exiting @ tick {} because {}'
      .format(m5.curTick(), exit_event.getCause()))