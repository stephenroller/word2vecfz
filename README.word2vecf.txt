
Producing embeddings with word2vecf:
====

There are three stages:

      1. Create input data, which is in the form of (word,context) pairs.
         the input data is a file in which each line has two space-separated items, 
         first is the word, second is the context.

         for example, in order to create syntactic contexts based on a dependency 
         parsed data in conll format:

            cut -f 2 conll_file | python scripts/vocab.py 50 > counted_vocabulary
            cat conll_file | python scripts/extract_deps.py counted_vocabulary 100 > dep.contexts

         (This part will take a while, and produce a very large file.)

         the first line counts how many times each word appears in the conll_file, 
         keeping all counts >= 50

         the second line extracts dependency contexts from the parsed file,
         skipping either words or contexts with words that appear < 100 times in
         the vocabulary.  (Note: currently, the `extract_deps.py` script is lowercasing the input.)

      1.5 If you want to perform sub-sampling, or prune away some examples, now will be a good time
          to do so.

      2. Create word and context vocabularies:

            ./myword2vec/count_and_filter -train dep.contexts -cvocab cv -wvocab wv -min-count 100

         This will count the words and contexts in dep.contexts, discard either words or contexts
         appearing < 100 times, and write the counted words to `wv` and the counted contexts to `cv`.
      
      3. Train the embeddings:
         
            ./myword2vec/word2vecf -train dep.contexts -wvocab wv -cvocab cv -output dim200vecs -size 200 -negative 15 -threads 10

         This will train 200-dim embeddings based on `dep.contexts`, `wv` and `cv` (lines in `dep.contexts` with word not in `wv` or context
         not in `cv` are ignored).
         
         The -dumpcv flag can be used in order to dump the trained context-vectors as well.

            ./myword2vec/word2vecf -train dep.contexts -wvocab wv -cvocab cv -output dim200vecs -size 200 -negative 15 -threads 10 -dumpcv dim200context-vecs


      3.5 convert the embeddins to numpy-readable format:
         
            ./scripts/vecs2nps.py dim200vecs vecs

          This will create `vecs.npy` and `vecs.vocab`, which can be read by
          the infer.py script.
