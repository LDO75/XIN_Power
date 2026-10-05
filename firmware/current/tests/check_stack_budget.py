"""Check linked RAM, stack allocation and compiler call graph before release.

Run after every firmware link. The reserve is a budget for exception frames,
callbacks and untraceable paths; static analysis is not runtime high-water data.
"""
from pathlib import Path
import re, sys
root=Path(__file__).resolve().parents[1]
mdk=root/'02_Application/MDK-ARM'
callgraph=Path(sys.argv[1]) if len(sys.argv)>1 else mdk/'Objects/XIN_Power.htm'
linkmap=Path(sys.argv[2]) if len(sys.argv)>2 else mdk/'Listings/XIN_Power.map'
allocated=int(re.search(r'Stack_Size\s+EQU\s+(0x[0-9a-fA-F]+)',(mdk/'startup_py32f403.s').read_text()).group(1),16)
used=int(re.search(r'Maximum Stack Usage =\s+(\d+) bytes',callgraph.read_text()).group(1))
maptext=linkmap.read_text()
# The map reports stack section size in decimal.
m=re.search(r'\n\s*STACK\s+0x([0-9a-fA-F]+)\s+Section\s+(\d+)',maptext)
base=int(m.group(1),16);size=int(m.group(2))
sp=int(re.search(r'__initial_sp\s+0x([0-9a-fA-F]+)',maptext).group(1),16)
ram=int(re.search(r'Execution Region RW_IRAM1 .*?Size: 0x([0-9a-fA-F]+)',maptext).group(1),16)
assert size==allocated and sp==base+size and sp%8==0
assert sp<=0x20010000 and ram<=65536, 'linked RAM exceeds hardware'
reserve=2048
assert used+reserve<=allocated, f'unsafe stack budget: {used} + {reserve} reserve > {allocated}'
print(f'PASS: stack={allocated}, static maximum={used}, reserve={reserve}, margin={allocated-used-reserve}; RAM={ram}/65536; SP=0x{sp:08X}')
