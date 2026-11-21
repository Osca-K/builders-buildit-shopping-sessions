import os
from pathlib import Path
import subprocess
import sys

data=Path(sys.argv[1]).resolve()
output=Path(sys.argv[2]).resolve()
output.chmod(0o777)
scratch=output/'scratch'
scratch.mkdir(exist_ok=True)
scratch.chmod(0o777)
subprocess.run(['docker','run','--rm','--network','none','--read-only',
    '--cpus',str(min(8,os.cpu_count() or 2)),'--memory','12g','--memory-swap','12g','--user','1000:1000',
    '--tmpfs','/tmp:rw,size=64m','-e','DATA_DIR=/data','-e','OUTPUT_DIR=/output',
    '-e','SCRATCH_DIR=/output/scratch','-e','HOME=/output/scratch','-e','NCPU=4','-e','MEM_GB=12',
    '-v',f'{data}:/data:ro','-v',f'{output}:/output','--entrypoint','sh','shopping',
    '-c','umask 000; exec /bin/sh /app/run_compiled.sh'],check=True)
