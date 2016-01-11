import sys
from infer import Embeddings

questions = file("/home/yogo/exps/vectorregularities2/data/google-analogies")
e = Embeddings.load(sys.argv[1])

for line in questions:
   cat, q1,q2,q3,a = line.lower().strip().split()
   try:
      print cat,q1,q2,q3,a,e.analogy(q2,q1,q3,1)[1]
   except:
      print "MISSING"
