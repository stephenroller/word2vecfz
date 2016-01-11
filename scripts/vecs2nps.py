import numpy as np
import sys

fh=file(sys.argv[1])
foutname=sys.argv[2]
first=fh.next()
size=map(int,first.strip().split())

wvecs=np.zeros((size[0],size[1]),float)

vocab=[]
for i,line in enumerate(fh):
    line = line.strip().split()
    vocab.append(line[0])
    wvecs[i,] = np.array(map(float,line[1:]))

np.save(foutname+".npy",wvecs)
with file(foutname+".vocab","w") as outf:
   print >> outf, " ".join(vocab)
