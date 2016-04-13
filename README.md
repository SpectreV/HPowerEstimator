# HPowerEstimator
FPGA is a promising hardware accelerator in modern
high-performance computing systems, e.g. cloud computing,
big-data processing, etc. In such a system, power is a key factor of
the design requiring thermal and energy-saving considerations.
Modern power estimators for FPGA either support specific
hardware provided by vendors or contain power models for
certain types of conventional FPGA architectures. However, with
technology advancement, novel FPGA of versatile architectures
are introduced to further augment current FPGA architecture
at various aspects, such as emerging FPGA with non-volatile
memory, nanowire interconnection of reconfigurable array, etc.
To evaluate the power consumption of various FPGA designs, the
power estimator has to be made more flexible and extendable
for supporting new devices and architectures. We introduced
a novel power estimator with hierarchical library supporting
power models at different levels, e.g. novel circuit of components,
emerging memory devices, architecture of time-multiplexing
fashion, etc. The power estimator also supports coarse-grain
or fine-grain power estimation defined by users for achieving
complexity-accuracy trade-off. Simulation results of benchmarks
of our power estimator against commercial one demonstrate
accuracy of our tool. Meanwhile, we showed two examples
of power estimation, RRAM FPGA and partial reconfigurable
FPGA, which have novel memory device and unconventional
operation, respectively. Our tool demonstrates flexibility to well
support, but not limited to, the new designs.
