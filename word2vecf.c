// TODO: add total word count to vocabulary, instead of "train_words"
//
// Modifed by Yoav Goldberg, Jan-Feb 2014
// Removed:
//    hierarchical-softmax training
//    cbow
// Added:
//   - support for different vocabularies for words and contexts
//   - different input syntax
//
/////////////////////////////////////////////////////////////////
//
//  Copyright 2013 Google Inc. All Rights Reserved.
//
//  Licensed under the Apache License, Version 2.0 (the "License");
//  you may not use this file except in compliance with the License.
//  You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <bzlib.h>
#include "vocab.h"
#include "io.h"

#define MAX_STRING 100
#define EXP_TABLE_SIZE 1000
#define MAX_EXP 6
#define MAX_SENTENCE_LENGTH 1000
#define MAX_CODE_LENGTH 40
#define FILE_BUFFER 1048576

// Precision of float numbers
typedef float real;

char train_file[MAX_STRING], output_file[MAX_STRING];
char wvocab_file[MAX_STRING], cvocab_file[MAX_STRING];
char dumpcv_file[MAX_STRING];
int binary = 0, debug_mode = 2, window = 5, min_count = 5, num_threads = 1, min_reduce = 1, use_position = 0;
long long layer1_size = 100;
long long train_words = 0, word_count_actual = 0;
real alpha = 0.025, starting_alpha, sample = 0;
real *syn0, *syn1, *syn1neg, *expTable;
clock_t start;

struct vocabulary *wv;
struct vocabulary *cv;

int negative = 15;
const int table_size = 1e8;
int *unitable;

// lock and stream for reading from corpus
pthread_mutex_t mutex;
BZFILE *inpt_strm;
char file_buff[FILE_BUFFER];
int bufidx = 0;
int bufmax = 0;
int read_everything = 0;

int read_data(char* buffer) {
  int n = 0;
  int bzerror = BZ_OK;

  // make sure two threads aren't reading simultaneously
  pthread_mutex_lock(&mutex);

  // find the last splittable spot in our buffer
  for (n = bufmax - 1; n > 0; n--) {
    if (file_buff[n] == '\n') {
      break;
    }
  }
  n++;

  strncpy(buffer, file_buff, n);
  buffer[n] = '\0';
  // copy the leftovers to the front of the buffer and refill buffer
  int leftover = bufmax - n;
  //printf("\ncopied %d leftovers to beginning of buffer\n", leftover);
  strncpy(file_buff, &file_buff[n], leftover);
  if (!read_everything) {
    bufmax = BZ2_bzRead(&bzerror, inpt_strm, &file_buff[leftover], FILE_BUFFER - leftover) + leftover;
  } else {
    bufmax = leftover;
  }

  if (bzerror == BZ_OK) {
    // pass
  } else if (bzerror == BZ_STREAM_END) {
    read_everything = 1;
  } else {
    fprintf(stderr, "BZ error, very unfortunate (%d).\n", bzerror);
    n = 0;
  }

  // free up the lock
  pthread_mutex_unlock(&mutex);
  if (n < 0) n = 0;
  return n;
}

// Used for sampling of negative examples.
// wc[i] == the count of context number i
// wclen is the number of entries in wc (context vocab size)
void InitUnigramTable(struct vocabulary *v) {
  int a, i;
  long long normalizer = 0;
  real d1, power = 0.75;
  unitable = (int *)malloc(table_size * sizeof(int));
  for (a = 0; a < v->vocab_size; a++) normalizer += pow(v->vocab[a].cn, power);
  i = 0;
  d1 = pow(v->vocab[i].cn, power) / (real)normalizer;
  for (a = 0; a < table_size; a++) {
    unitable[a] = i;
    if (a / (real)table_size > d1) {
      i++;
      d1 += pow(v->vocab[i].cn, power) / (real)normalizer;
    }
    if (i >= v->vocab_size) i = v->vocab_size - 1;
  }
}

void InitNet(struct vocabulary *wv, struct vocabulary *cv) {
   long long a, b;
   a = posix_memalign((void **)&syn0, 128, (long long)wv->vocab_size * layer1_size * sizeof(real));
   if (syn0 == NULL) {printf("Memory allocation failed\n"); exit(1);}
   for (b = 0; b < layer1_size; b++) 
      for (a = 0; a < wv->vocab_size; a++)
         syn0[a * layer1_size + b] = (rand() / (real)RAND_MAX - 0.5) / layer1_size;

   a = posix_memalign((void **)&syn1neg, 128, (long long)cv->vocab_size * layer1_size * sizeof(real));
   if (syn1neg == NULL) {printf("Memory allocation failed\n"); exit(1);}
   for (b = 0; b < layer1_size; b++)
      for (a = 0; a < cv->vocab_size; a++)
        syn1neg[a * layer1_size + b] = 0;
}

// Read word,context pairs from training file, where both word and context are integers.
// We are learning to predict context based on word.
//
// Word and context come from different vocabularies, but we do not really care about that
// at this point.
void *TrainModelThread(void *id) {
  int ctxi = -1, wrdi = -1;
  long long d;
  long long word_count = 0, last_word_count = 0;
  long long l1, l2, c, target, label;
  unsigned long long next_random = (unsigned long long)id;
  real f, g;
  clock_t now;
  real *neu1 = (real *)calloc(layer1_size, sizeof(real));
  real *neu1e = (real *)calloc(layer1_size, sizeof(real));

  char buffer[FILE_BUFFER];
  char word[MAX_STRING];
  int i = 0, swtch = 0, buflen = 0, j = 0;

  long long train_words = wv->word_count;
  while (1) { //HERE @@@
    // TODO set alpha scheduling based on number of examples read.
    // The conceptual change is the move from word_count to pair_count
    if (word_count - last_word_count > 10000) {
        word_count_actual += word_count - last_word_count;
        last_word_count = word_count;
        if ((debug_mode > 1)) {
          now=clock();
          printf("%cAlpha: %f  Progress: %.2f%%  Words/thread/sec: %.2fk  ", 13, alpha,
                word_count_actual / (real)(train_words + 1) * 100,
                word_count_actual / ((real)(now - start + 1) / (real)CLOCKS_PER_SEC * 1000));
          fflush(stdout);
        }
        alpha = starting_alpha * (1 - word_count_actual / (real)(train_words + 1));
        if (alpha < starting_alpha * 0.0001) alpha = starting_alpha * 0.0001;
    }

    while (1) {
      if (i >= buflen) {
        buflen = read_data(buffer);
        if (buflen <= 0) break;
        i = 0;
      }
      c = buffer[i++];
      if (c == '\t' || c == '\n') {
        word[j] = '\0';
        j = 0;
        swtch = !swtch;
        if (swtch) {
          wrdi = SearchVocab(wv, word);
          if (wrdi < 0) {
            printf("Failed to find wv '%s'\n", word);
          }
        } else {
          ctxi = SearchVocab(cv, word);
          if (ctxi < 0) {
            printf("Failed to find cv '%s'\n", word);
          }
          break;
        }
      } else {
        word[j++] = c;
      }
    }
    if (buflen <= 0) {
      break;
    }

    for (c = 0; c < layer1_size; c++) neu1[c] = 0;
    for (c = 0; c < layer1_size; c++) neu1e[c] = 0;

    word_count++; //TODO ?
    if (wrdi < 0 || ctxi < 0) continue;

    if (sample > 0) {
        real ran = (sqrt(wv->vocab[wrdi].cn / (sample * wv->word_count)) + 1) * (sample * wv->word_count) / wv->vocab[wrdi].cn;
        next_random = next_random * (unsigned long long)25214903917 + 11;
        if (ran < (next_random & 0xFFFF) / (real)65536) continue;
        ran = (sqrt(cv->vocab[ctxi].cn / (sample * cv->word_count)) + 1) * (sample * cv->word_count) / cv->vocab[ctxi].cn;
        next_random = next_random * (unsigned long long)25214903917 + 11;
        if (ran < (next_random & 0xFFFF) / (real)65536) continue;
    }
    //
    // NEGATIVE SAMPLING
    l1 = wrdi * layer1_size;
    for (d = 0; d < negative + 1; d++) {
        if (d == 0) {
          target = ctxi;
          label = 1;
        } else {
          next_random = next_random * (unsigned long long)25214903917 + 11;
          target = unitable[(next_random >> 16) % table_size];
          if (target == 0) target = next_random % (cv->vocab_size - 1) + 1;
          if (target == ctxi) continue;
          label = 0;
        }
        l2 = target * layer1_size;
        f = 0;
        for (c = 0; c < layer1_size; c++) f += syn0[c + l1] * syn1neg[c + l2];
        if (f > MAX_EXP) g = (label - 1) * alpha;
        else if (f < -MAX_EXP) g = (label - 0) * alpha;
        else g = (label - expTable[(int)((f + MAX_EXP) * (EXP_TABLE_SIZE / MAX_EXP / 2))]) * alpha;
        for (c = 0; c < layer1_size; c++) neu1e[c] += g * syn1neg[c + l2];
        for (c = 0; c < layer1_size; c++) syn1neg[c + l2] += g * syn0[c + l1];
    }
    // Learn weights input -> hidden
    for (c = 0; c < layer1_size; c++) syn0[c + l1] += neu1e[c];
  }
  free(neu1);
  free(neu1e);
  pthread_exit(NULL);
}

void TrainModel() {
  long a, b;
  FILE *fo;
  FILE *fo2;
  pthread_t *pt = (pthread_t *)malloc(num_threads * sizeof(pthread_t));
  printf("Starting training using file %s\n", train_file);
  starting_alpha = alpha;
  wv = ReadVocab(wvocab_file);
  cv = ReadVocab(cvocab_file);
  InitNet(wv, cv);
  InitUnigramTable(cv);

  // initialize the stream and the mutex
  int bzerror = BZ_OK;
  FILE *inptfile = fopen(train_file, "rb");
  inpt_strm = BZ2_bzReadOpen(&bzerror, inptfile, 0, 0, NULL, 0);
  bufmax = BZ2_bzRead(&bzerror, inpt_strm, &file_buff, FILE_BUFFER);
  if (bzerror != BZ_OK) {
    fprintf(stderr, "Some BZ2 error, sorry.\n");
    exit(1);
  }
  pthread_mutex_init(&mutex, NULL);


  // start all the threads
  start = clock();
  for (a = 0; a < num_threads; a++) pthread_create(&pt[a], NULL, TrainModelThread, (void *)a);
  // join all threads
  for (a = 0; a < num_threads; a++) pthread_join(pt[a], NULL);
  // all done with main computation,
  // let's clean up
  BZ2_bzReadClose(&bzerror, inpt_strm);
  fclose(inptfile);
  pthread_mutex_destroy(&mutex);

  fo = fopen(output_file, "wb");
  // Save the word vectors
  if (dumpcv_file[0] != 0) {
      fo2 = fopen(dumpcv_file, "wb");
      fprintf(fo2, "%ld %lld\n", cv->vocab_size, layer1_size);
      for (a = 0; a < cv->vocab_size; a++) {
          fprintf(fo2, "%s ", cv->vocab[a].word); //TODO
          if (binary) for (b = 0; b < layer1_size; b++) fwrite(&syn1neg[a * layer1_size + b], sizeof(real), 1, fo2);
          else for (b = 0; b < layer1_size; b++) fprintf(fo2, "%lf ", syn1neg[a * layer1_size + b]);
          fprintf(fo2, "\n");
      }
  }
  fprintf(fo, "%ld %lld\n", wv->vocab_size, layer1_size);
  for (a = 0; a < wv->vocab_size; a++) {
    fprintf(fo, "%s ", wv->vocab[a].word); //TODO
    if (binary) for (b = 0; b < layer1_size; b++) fwrite(&syn0[a * layer1_size + b], sizeof(real), 1, fo);
    else for (b = 0; b < layer1_size; b++) fprintf(fo, "%lf ", syn0[a * layer1_size + b]);
    fprintf(fo, "\n");
  }
  fclose(fo);
}

int ArgPos(char *str, int argc, char **argv) {
  int a;
  for (a = 1; a < argc; a++) if (!strcmp(str, argv[a])) {
    if (a == argc - 1) {
      printf("Argument missing for %s\n", str);
      exit(1);
    }
    return a;
  }
  return -1;
}

int main(int argc, char **argv) {
  int i;
  if (argc == 1) {
    printf("WORD VECTOR estimation toolkit v 0.2\n\n");
    printf("Options:\n");
    printf("Parameters for training:\n");
    printf("\t-train <file>\n");
    printf("\t\tUse text data from <file> to train the model. *Must* be bz2 compressed.\n");
    printf("\t-output <file>\n");
    printf("\t\tUse <file> to save the resulting word vectors / word clusters\n");
    printf("\t-size <int>\n");
    printf("\t\tSet size of word vectors; default is 100\n");
    printf("\t-negative <int>\n");
    printf("\t\tNumber of negative examples; default is 15, common values are 5 - 10 (0 = not used)\n");
    printf("\t-threads <int>\n");
    printf("\t\tUse <int> threads (default 1)\n");
    printf("\t-sample <float>\n");
    printf("\t\tSet threshold for occurrence of words and contexts. Those that appear with higher frequency");
    printf(" in the training data will be randomly down-sampled; default is 0 (off), useful value in the original word2vec was 1e-5\n");
    printf("\t-alpha <float>\n");
    printf("\t\tSet the starting learning rate; default is 0.025\n");
    printf("\t-binary <int>\n");
    printf("\t\tSave the resulting vectors in binary moded; default is 0 (off)\n");
    printf("\t-dumpcv filename\n");
    printf("\t\tDump the context vectors in file <filename>\n");
    printf("\t-wvocab filename\n");
    printf("\t\twords vocabulary file\n");
    printf("\t-cvocab filename\n");
    printf("\t\tcontexts vocabulary file\n");
    printf("\nExamples:\n");
    printf("./word2vecf -train data.txt -wvocab wv -cvocab cv -output vec.txt -size 200 -negative 5 -threads 10 \n\n");
    return 0;
  }
  output_file[0] = 0;
  wvocab_file[0] = 0;
  cvocab_file[0] = 0;
  dumpcv_file[0] = 0;
  if ((i = ArgPos((char *)"-size", argc, argv)) > 0) layer1_size = atoi(argv[i + 1]);
  if ((i = ArgPos((char *)"-train", argc, argv)) > 0) strcpy(train_file, argv[i + 1]);
  if ((i = ArgPos((char *)"-wvocab", argc, argv)) > 0) strcpy(wvocab_file, argv[i + 1]);
  if ((i = ArgPos((char *)"-cvocab", argc, argv)) > 0) strcpy(cvocab_file, argv[i + 1]);
  if ((i = ArgPos((char *)"-debug", argc, argv)) > 0) debug_mode = atoi(argv[i + 1]);
  if ((i = ArgPos((char *)"-binary", argc, argv)) > 0) binary = atoi(argv[i + 1]);
  if ((i = ArgPos((char *)"-alpha", argc, argv)) > 0) alpha = atof(argv[i + 1]);
  if ((i = ArgPos((char *)"-output", argc, argv)) > 0) strcpy(output_file, argv[i + 1]);
  if ((i = ArgPos((char *)"-negative", argc, argv)) > 0) negative = atoi(argv[i + 1]);
  if ((i = ArgPos((char *)"-threads", argc, argv)) > 0) num_threads = atoi(argv[i + 1]);
  if ((i = ArgPos((char *)"-min-count", argc, argv)) > 0) min_count = atoi(argv[i + 1]);
  if ((i = ArgPos((char *)"-sample", argc, argv)) > 0) sample = atof(argv[i + 1]);
  if ((i = ArgPos((char *)"-dumpcv", argc, argv)) > 0) strcpy(dumpcv_file, argv[i + 1]);

  if (output_file[0] == 0) { printf("must supply -output.\n\n"); return 0; }
  if (wvocab_file[0] == 0) { printf("must supply -wvocab.\n\n"); return 0; }
  if (cvocab_file[0] == 0) { printf("must supply -cvocab.\n\n"); return 0; }
  expTable = (real *)malloc((EXP_TABLE_SIZE + 1) * sizeof(real));
  for (i = 0; i < EXP_TABLE_SIZE; i++) {
    expTable[i] = exp((i / (real)EXP_TABLE_SIZE * 2 - 1) * MAX_EXP); // Precompute the exp() table
    expTable[i] = expTable[i] / (expTable[i] + 1);                   // Precompute f(x) = x / (x + 1)
  }
  TrainModel();
  return 0;
}
