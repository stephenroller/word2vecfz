import sys
from collections import Counter
wc = Counter()
thr = int(sys.argv[1])
l = []
for i,w in enumerate(sys.stdin):
   if i % 1000000 == 0:
      #if i > 10000000: break
      print >> sys.stderr,i,len(wc)
      wc.update(l)
      l = []
   l.append(w)
wc.update(l)

for w,c in sorted([(w,c) for w,c in wc.iteritems() if c >= thr and w != ''],key=lambda x:-x[1]):
   print "\t".join([w.strip(),str(c)])

