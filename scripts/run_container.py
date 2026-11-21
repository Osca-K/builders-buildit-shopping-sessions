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
    '--cpus','8','--memory','24g','--memory-swap','24g','--user','1000:1000',
    '--tmpfs','/tmp:rw,size=64m','-e','DATA_DIR=/data','-e','OUTPUT_DIR=/output',
    '-e','SCRATCH_DIR=/output/scratch','-e','HOME=/output/scratch',
    '-v',f'{data}:/data:ro','-v',f'{output}:/output','shopping'],check=True)
