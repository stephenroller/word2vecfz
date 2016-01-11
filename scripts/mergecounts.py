import sys
from collections import defaultdict
wc = defaultdict(int)
for line in sys.stdin:
   line = line.strip().split()
   if len(line)!=2:
      print >> sys.stderr,"skipping line",line
      continue
   wc[line[0]]+=int(line[1])
for w,c in sorted(wc.iteritems(),key=lambda x:-x[1]):
   print w,c


