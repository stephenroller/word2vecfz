# extract dependency pairs from a conll file.
# assumes google universal-treebank annotation scheme.
# zcat treebank.gz |python extract_deps.py |gzip - > deps.gz
import sys
from collections import defaultdict

vocab_file = sys.argv[1]
try:
   THR = int(sys.argv[2])
except IndexError: THR=100

lower=True

def read_conll(fh):
   root = (0,'*root*',-1,'rroot')
   tokens = [root]
   for line in fh:
      if lower: line = line.lower()
      tok = line.strip().split()
      if not tok:
         if len(tokens)>1: yield tokens
         tokens = [root]
      else:
         tokens.append((int(tok[0]),tok[1],int(tok[-4]),tok[-3]))
   if len(tokens) > 1:
      yield tokens

def read_vocab(fh):
   v = {}
   for line in fh:
      if lower: line = line.lower()
      line = line.strip().split()
      if len(line) != 2: continue
      if int(line[1]) >= THR:
         v[line[0]] = int(line[1])
   return v

vocab = set(read_vocab(file(vocab_file)).keys())
print >> sys.stderr,"vocab:",len(vocab)
for i,sent in enumerate(read_conll(sys.stdin)):
   if i % 100000 == 0: print >> sys.stderr,i
   #d = defaultdict(list)
   for tok in sent[1:]:
      par = sent[tok[2]]
      m = tok[1]
      if m not in vocab: continue
      rel = tok[3]
      if rel == 'adpmod': continue # this is the prep. we'll get there (or the PP is crappy)
      if rel == 'adpobj' and par[0] != 0:
         ppar = sent[par[2]]
         rel = "%s:%s" % (par[3],par[1])
         h = ppar[1]
      else:
         h = par[1]
      if h not in vocab: continue
      print h,"_".join((rel,m))
      print m,"I_".join((rel,h))
      #d[h].append("_".join((rel,m)))
      #d[m].append("I_".join((rel,h)))
   #for w,cs in d.iteritems():
   #   print w," ".join(cs)




