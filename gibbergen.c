;/*
  Short:    gibbergen - Generate language specific pronounceable non-dictionary words
  Author:   Mark J Swift
  Version:  2.0.7

  DESCRIPTION:

  A command line utility that generates new pronouncable words in your language
  of choice. It  builds language rules based on a template text file, then
  generates random words based on those rules. For example, if the template file
  is in french, gibbergen will generate french-sounding words. 
  Generated words will not include words from the original template file.
  A text file of words-to-exclude can optionally be supplied to prevent generated
  words from being in an existing dictionary.

  HISTORY:

  2.0.7 - 18 FEB 2015
  * Bug fix. Didn't print to stdout if the -l or -b options were used.

  2.0.6 - 15 FEB 2015
  * First public release.

  Pre 2.0.6 - SOME TIME AGO
  * Originally written on an Amiga and based on someones example of how to store 
    dictionary words in hash chains. If I could remember the original example, 
    I would reference it.
*/

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>

#define FALSE	 0
#define TRUE	 1

#define RNDSEED 17							/* values for random number generator */
#define RNDINC  1
#define RNDMUL  2005
#define RNDMOD  32768

#define INITRCHAIN 1024 					/* initial size of char rules chain */
#define GROWRCHAIN 1024 					/* char rules chain growth rate*/

#define DHASHRINC   1						/* values for dictionary hash generator */
#define DHASHRMUL   857

#define DHASHSIZE   2048					/* Entries in hash table */

#define INITDCHAIN  48						/* Initial slots in hash items */
#define GROWDCHAIN  16						/* Initial slots in hash items */

#define INITDWORDS 24576					/* Initial word list length */
#define GROWDWORDS 8192 					/* Word list growth increment */

#define INITDWORDBUF 196608				/* initial size for dictionary word buffer r*/
#define GROWDWORDBUF 65536 				/* dictionary word buffer growth increment */

#define DEFAULT_SRCWORDLEN 5				/* source words smaller than this are ignored */
#define DEFAULT_GIBBERCOUNT 8192 		/* count of gibberish words */
#define DEFAULT_GIBBERMINLEN 6 		/* min no chars in gibberish words */
#define DEFAULT_GIBBERMAXLEN 8 		/* max no chars in gibberish words */


/* rough translations for foreign characters */

unsigned char isotoalpha1[64] = {
	65,65,65,65,65,65,65,67,69,69,69,69,73,73,73,73,
	68,78,79,79,79,79,79,88,79,85,85,85,85,89,80,83,
	97,97,97,97,97,97,97,99,101,101,101,101,105,105,105,105,
	100,110,111,111,111,111,111,120,111,117,117,117,117,121,112,121
};

unsigned char isotoalpha2[64] = {
	0,0,0,0,0,0,69,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,83,
	0,0,0,0,0,0,101,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

static int VERBOSE = FALSE;

static int VERBOSE2 = FALSE;

static char stringbuf[1025];						/* String I/O buffer */

static unsigned int srcwordlen;

static unsigned int gibbercharcount;
static unsigned int gibbercharpairhash;
static unsigned long gibberrandnum;

struct useageentry { 								/* Hash table item */
  unsigned long nchars, maxchars;							/* Number of chars in chain, max chars*/
  unsigned char charchain[INITRCHAIN]; 		/* Char chain for this hash*/
};

static struct useageentry **useagerules;		/* Hashed table for char usage rules */

struct dictwords {
  unsigned long dwordbufpos,dwordbufmax;					/* Next/Max position in word buffer */
  char *dictwordbuf; 								/* Character buffer for words */
  unsigned long nwords, maxwords;							/* Number/Max... words in dictionary */
  unsigned long dword[INITDWORDS];							/* Index into word buffer */
};

struct dicthashentry {								/* Hash chain entry for a word */
  unsigned long hnum;											/* Index into word array */
};

struct dicthashchain {								/* Dictionary hash chain */
  unsigned long nitems, maxitems;							/* Number of entries in chain and max */
  struct dicthashentry hitem[INITDCHAIN]; 	/* Hash chain entries */
};

struct dictionary {									/* Dictionary definition */
  struct dictwords *words;
  struct dicthashchain *hchain[DHASHSIZE];	/* Hash chains */
};

static struct dictionary *languagedict;		/* Dictionary of source words */
static struct dictionary *gibberdict;			/* Dictionary of gibberish */
static struct dictionary *exclusiondict;		/* Dictionary of words to exclude */


/*  DHASHCODE	--  Compute dictionary hash code for string. */

static unsigned long dhashcode(char *s)
{
  unsigned long h = 0x0;

  while (*s)
	 h = ((h + DHASHRINC) * DHASHRMUL + *s++) % DHASHSIZE;

  return (unsigned long) h;
}


/*  dictentry	--  find position of word in dictionary.	*/

static unsigned long dictentry(struct dictionary *dict, char *w)
{
  unsigned long h;
  unsigned long i;
  struct dicthashchain *hchain;
  struct dictwords *words;

  if (VERBOSE)
	 fprintf(stderr, "Checking dictionary for word %s.\n",w);

  h = dhashcode(w);

  if ((hchain = dict->hchain[h]) != NULL)
  {
	 words = dict->words;

	 for (i = 0; i < hchain->nitems; i++)
	 {
		if (strcmp(&words->dictwordbuf[words->dword[hchain->hitem[i].hnum]], w) == 0)
		{
		  if (VERBOSE)
			 fprintf(stderr,
						"Found word %s in dictionary at position %ld.\n",
						w, hchain->hitem[i].hnum);
		  return hchain->hitem[i].hnum;
		}
	 }
  }

  return -1;
}


/*  addtodict	--  add a word to a dictionary.	*/

static void addtodict(struct dictionary *dict, char *w)
{
  unsigned long h;
  struct dicthashchain *hchain;
  struct dictwords *words;

  if (VERBOSE)
	 fprintf(stderr, "Considering %s for dictionary.\n",w);

  /* Allocate dictionary word structure if not already present. */

  if ((words = dict->words) == NULL)
  {
	 if (VERBOSE)
		fprintf(stderr, "Allocating structure for dictionary words.\n");

	 dict->words = words = (struct dictwords *) calloc(1, sizeof(struct dictwords));
	 words->nwords = 0;
	 words->maxwords = INITDWORDS;
  }

  if ((words->dictwordbuf) == NULL)
  {
	 words->dictwordbuf = (char *) malloc(INITDWORDBUF * sizeof(char));
	 words->dwordbufpos = 0;
	 words->dwordbufmax = INITDWORDBUF;

	 if (VERBOSE2)
		fprintf(stderr, "Allocating buffer size of %ld for dictionary of %ld words.(%s)\n",words->dwordbufmax,words->nwords,w);
  }

  h = dhashcode(w);

  /* Allocate hash item if not already present. */

  if ((hchain = dict->hchain[h]) == NULL)
  {
	 if (VERBOSE)
		fprintf(stderr, "Allocating hash chain %ld.\n",h);

	 dict->hchain[h] = hchain = (struct dicthashchain *) calloc(1, sizeof(struct dicthashchain));
	 hchain->nitems = 0;
	 hchain->maxitems = INITDCHAIN;
  }


  /* make sure word isn't a duplicate */

  if (dictentry(dict,w) != -1)
  {
	 if (VERBOSE)
		fprintf(stderr,"Word %s is already in dictionary.\n",w);
  }
  else
  {
	 /* Add word to hash chain */

	 /* Expand hash item if necessary. */

	 if (hchain->nitems >= hchain->maxitems)
	 {
		dict->hchain[h] = hchain = (struct dicthashchain *) realloc(dict->hchain[h],
					 sizeof(struct dicthashchain) +
					 (sizeof(struct dicthashentry) *
					 ((hchain->maxitems - INITDCHAIN) + GROWDCHAIN)));
		hchain->maxitems += GROWDCHAIN;
		if (VERBOSE)
		  fprintf(stderr, "Hash chain %ld growing to %ld entries.\n",
					 h, hchain->maxitems);
	 }

	 if (VERBOSE)
		fprintf(stderr, "Adding %s to hash chain %ld.\n",w,h);

	 hchain->hitem[hchain->nitems++].hnum = words->nwords;


	 /* Add word to dictionary words */

	 /* Expand word array if necessary. */

	 if (words->nwords >= words->maxwords)
	 {
		dict->words = words = (struct dictwords *) realloc(dict->words,
					 sizeof(struct dictwords) +
					 (sizeof(unsigned long) *
					 ((words->maxwords - INITDWORDS) + GROWDWORDS)));
		words->maxwords += GROWDWORDS;

		if (VERBOSE2)
		  fprintf(stderr, "Words array growing to %ld entries.(%s)\n",
					 words->maxwords,w);
	 }

	 /* Expand dictionary word buffer if necessary. */

	 if ((words->dwordbufpos+strlen(w)+2) >= words->dwordbufmax)
	 {
		words->dictwordbuf = (char *) realloc(words->dictwordbuf,
												 (words->dwordbufmax + GROWDWORDBUF) * sizeof(char));
		words->dwordbufmax+=GROWDWORDBUF;

		if (VERBOSE2)
		  fprintf(stderr, "Expanding buffer to %ld for dictionary of %ld words.(%s)\n",
					 words->dwordbufmax,words->nwords,w);
	 }

	 if (VERBOSE)
		fprintf(stderr, "Adding %s to dictionary at position %ld.\n" ,w ,words->nwords);

	 words->dword[words->nwords++] = words->dwordbufpos;

	 strcpy (&words->dictwordbuf[words->dwordbufpos], w);
	 words->dwordbufpos+=(strlen(w)+1);
	 words->dwordbufpos=(words->dwordbufpos + 1) & ~1;

	 if (VERBOSE)
		fprintf(stderr, "word buffer pointer = %ld.\n",
				  words->dwordbufpos);
  }

}


/*  nextsourceword  --	Return next word from word source file */

static char *nextsourceword(FILE *fp)
{
  unsigned int thischar,c1,c2;
  static char *w = NULL;

  do
  {
	 w=stringbuf;

	 do
	 {
		thischar = (unsigned int)(fgetc(fp));

        if (thischar!=EOF)
        {        
			if (thischar > 191)
			{
				c1=isotoalpha1[thischar-192];
		  		c2=isotoalpha2[thischar-192];
			}
			else
			{
		  		c1=thischar;
		  		c2=0;
			}

			*w=tolower(c1);
			w++;
			if (c2!=0)
			{
		  		*w=tolower(c2);
		  		w++;
			}

        }
        
	 } while ((isalpha(*(w-1)))&&(thischar!=EOF));

	 *(w-1)=0;

  } while ((strlen(stringbuf)==0)&&(thischar!=EOF));

  if (thischar!=EOF)
	 return stringbuf;
  else
	 return NULL;

}


/*  loaddictionary  --	load dictionary words from file.  */

static void loaddictionary(struct dictionary *dict, char *wordsource)
{

  FILE *fp;
  static char *w = NULL;

  fp = fopen(wordsource, "r");
  if (fp == NULL)
  {
	 fprintf(stderr, "Cannot open word source file %s\n", wordsource);
	 return;
  }

  while ((w = nextsourceword(fp)) != NULL)
  {

	 /* add word to dictionary*/

	 if (strlen(w)>=srcwordlen)
		addtodict(dict, w);

  }

  fclose(fp);
}


/*  makeusagerules  --	make character usage rules from file.	*/

static void makeusagerules(char *wordsource)
{

  FILE *fp;
  unsigned int  prevchar,prevprev,thischar;
  unsigned int  charpairhash;
  static char *w = NULL;

  fp = fopen(wordsource, "r");
  if (fp == NULL)
  {
	 fprintf(stderr, "Cannot open word source file %s\n", wordsource);
	 return;
  }

  while ((w = nextsourceword(fp)) != NULL)
  {

	 if (strlen(w)>=srcwordlen)
	 {

		/* make sure duplicate words don't bias usage rules */

		if (dictentry(languagedict,w) != -1)
		{
		  if (VERBOSE)
			 fprintf(stderr,"Word %s is duplicated in language source file.\n",w);
		}
		else
		{

		  /* add word to language dictionary */

		  addtodict(languagedict,w);

		  /* add word to exclusion dictionary */

		  /* addtodict(exclusiondict,w); */

		  /* put a space after word */

		  w[strlen(w)+1]=0;
		  w[strlen(w)]=32;

		  prevprev = 32;
		  prevchar = 32;

		  while ( (thischar=(unsigned int)*w) )
		  {
			 charpairhash=(prevprev*256)+prevchar;

			 if (useagerules[charpairhash] == NULL)
			 {
				if (VERBOSE)
				  fprintf(stderr, "Creating char chain %u (%c %c).\n", charpairhash, prevprev, prevchar);

				useagerules[charpairhash] = (struct useageentry *) calloc(1, sizeof(struct useageentry));
				useagerules[charpairhash]->nchars = 0;
				useagerules[charpairhash]->maxchars = INITRCHAIN;
			 }

			 /* Expand char chain if necessary. */

			 if (useagerules[charpairhash]->nchars >= useagerules[charpairhash]->maxchars)
			 {
				if (VERBOSE)
				  fprintf(stderr, "Extending char chain %u (%c %c) to %ld.\n", charpairhash, prevprev, prevchar,
							 sizeof(struct useageentry) + ((useagerules[charpairhash]->maxchars - INITRCHAIN) + GROWRCHAIN)
							);

/* 	fgetc(stdin); */
				useagerules[charpairhash] = (struct useageentry *) realloc(useagerules[charpairhash],
													 sizeof(struct useageentry) +
													 ((useagerules[charpairhash]->maxchars - INITRCHAIN) + GROWRCHAIN));
				useagerules[charpairhash]->maxchars += GROWRCHAIN;
			 }

			 useagerules[charpairhash]->charchain[useagerules[charpairhash]->nchars] = (unsigned char)thischar;
			 useagerules[charpairhash]->nchars+=1;

			 prevprev = prevchar;
			 prevchar = thischar;

			 w++;
		  }
		}
	 }
  }

	if (VERBOSE)
		fprintf(stderr, "Closing source file %s\n", wordsource);

  fclose(fp);

}


/*  nextgibberchar  --	return next char of gibberish  */

static unsigned int nextgibberchar()
{
  unsigned int c;
  unsigned long n, m;

  if (useagerules[gibbercharpairhash] != NULL)
  {
	n = useagerules[gibbercharpairhash]->nchars;
        m = (unsigned long)(n/RNDMOD);
	c = useagerules[gibbercharpairhash]->charchain[(unsigned long)(((gibberrandnum * (n - (m * RNDMOD)))/RNDMOD) + (m * gibberrandnum))];
  }
  else
	c = 42;

  gibbercharpairhash = ((gibbercharpairhash * 256) + (unsigned int)c) % 65536;
  gibberrandnum = (gibberrandnum * RNDMUL + RNDINC + (gibbercharcount % RNDMUL)) % RNDMOD;
  gibbercharcount+=1;

  return c;
}


static char *nextgibberword()
{
  unsigned int thischar,c1,c2;
  static char *w = NULL;

  do
  {
	 w=stringbuf;

	 gibbercharpairhash = (32 * 256) + 32;

	 while (isalpha(*w=nextgibberchar()))
		w++;

	 *w=0;

  } while (strlen(stringbuf)==0);

  return stringbuf;
}


/*  makegibberdict  --	create dictionary of gibberish that conforms to usage rules.  */

static void makegibberdict(int gibberlimit, int gibberminlen, int gibbermaxlen)
{
  char *w;
  int i;

  do
  {

	 do
	 {
		/* generate a word of gibberish */
		w=nextgibberword();

		/* throw if word less than gibberminlen chars, or greater than */
		/* gibbermaxlen chars, or if word in dictionary of excluded words */

         if (VERBOSE2)
           if ((strlen(w) > gibberminlen) && (strlen(w) < gibbermaxlen) && (dictentry(languagedict,w) != -1) && (dictentry(exclusiondict,w) != -1))
	     fprintf(stderr, "excluding %s\n",w);


	 } while ((strlen(w) < gibberminlen) || (strlen(w) > gibbermaxlen) || (dictentry(languagedict,w) != -1) || (dictentry(exclusiondict,w) != -1));

	 /* add word to dictionary of gibberish */
	 addtodict(gibberdict,w);

  } while (gibberdict->words->nwords < gibberlimit);

}


/*  savedictionary  --	save dictionary words to file.  */

static void savedictionary(struct dictionary *dict, char *worddest)
{
  unsigned long i;
  FILE *fp;
  static char *w = NULL;

  fp = fopen(worddest, "wb");
  if (fp == NULL)
  {
	 fprintf(stderr, "Cannot open word output file %s\n", worddest);
	 return;
  }

  if (VERBOSE2)
	 fprintf(stderr, "Saving dictionay of %ld words.\n",dict->words->nwords);

  for (i=0 ; i<dict->words->nwords ; i++)
  {
	 fprintf(fp, "%s\n", &dict->words->dictwordbuf[dict->words->dword[i]]);
   }

  fclose(fp);
}


/*  printdictionary  --	print dictionary words to stdout.  */

static void printdictionary(struct dictionary *dict)
{
  unsigned long i;
  static char *w = NULL;

  if (VERBOSE2)
	 fprintf(stderr, "Printing dictionay of %ld words.\n",dict->words->nwords);

  for (i=0 ; i<dict->words->nwords ; i++)
  {
	 fprintf(stdout, "%s\n", &dict->words->dictwordbuf[dict->words->dword[i]]);
   }
}


/*  USAGE  --	call option information.  */

static void usage(char *pname)
{
	 fprintf(stderr, "\n");
	 fprintf(stderr, " gibbergen version 2.0.7:\n\n");

	 fprintf(stderr, " A command line utility that generates new pronouncable words in your language\n");
	 fprintf(stderr, " of choice. It  builds language rules based on a template text file, then\n");
	 fprintf(stderr, " generates random words based on those rules. For example, if the template file\n");
	 fprintf(stderr, " is in french, gibbergen will generate french-sounding words.\n");
	 fprintf(stderr, " Generated words will not include words from the original template file.\n");
	 fprintf(stderr, " A text file of words-to-exclude can optionally be supplied to prevent generated\n");
	 fprintf(stderr, " words from being in an existing dictionary.\n\n");
	 fprintf(stderr, " Call with %s [options]\n\n", pname);
	 fprintf(stderr, " Options (must be specified in this order):\n");
	 fprintf(stderr, "         -t file        Build language rules from this text file\n");
	 fprintf(stderr, "         -x file        Exclude all words in this text file from generated words\n");
	 fprintf(stderr, "         -l file        Save unique words from language file(s) as dictionary\n");
	 fprintf(stderr, "         -b file        Save all excluded (bad) words to file as dictionary\n");
	 fprintf(stderr, "         -n n           Generate words no shorter than this many characters\n");
	 fprintf(stderr, "         -m n           Generate words no longer than this many characters\n");
	 fprintf(stderr, "         -c n           Count. Generate this many unique words\n");
	 fprintf(stderr, "         -f file        Output generated words to file instead of stdout\n");
	 fprintf(stderr, "         -v             Be verbose\n");
	 fprintf(stderr, "         -w             Be very verbose\n");
	 fprintf(stderr, "         -h             Print this message\n\n");

	 exit(0);
}


/*  Main program.  */

int main(int argc, char *argv[])
{
  int i = 1;
  char *cp, *arg, opt;
  static char *infile, *outfile;
  static char *wordsource,*worddest;

  unsigned long gibberlimit, gibberminlen, gibbermaxlen;

  #define CheckArg arg = ((i < argc) && ( *argv[i] != '-' ))? argv[i] : NULL; if (arg == NULL) { fprintf(stderr, "Argument missing for -%c flag.\n", opt); } else i++

  useagerules = (struct useageentry **) calloc(256 * 256, sizeof(struct useageentry *));

  languagedict = (struct dictionary *) calloc(1, sizeof(struct dictionary));
  gibberdict = (struct dictionary *) calloc(1, sizeof(struct dictionary));
  exclusiondict = (struct dictionary *) calloc(1, sizeof(struct dictionary));

  gibberlimit = DEFAULT_GIBBERCOUNT;
  gibberminlen = DEFAULT_GIBBERMINLEN;
  gibbermaxlen = DEFAULT_GIBBERMAXLEN;

  gibbercharcount = 0;
  gibbercharpairhash = (32 * 256) + 32;
  gibberrandnum = RNDSEED;

  srcwordlen = DEFAULT_SRCWORDLEN;

  while (i < argc)
  {
	 cp = argv[i];
	 if (*cp == '-' && cp[1])
	 {
		opt = *(++cp);
		if (islower(opt))
		  opt = toupper(opt);

		i++;
	 }
	 else
	 {
		opt = 'E';
	 }

	 switch (opt)
	 {
		case 'T':
		/* Scan text file, create char usage rules */
		CheckArg;
		wordsource = arg;
		makeusagerules(wordsource);
		break;

		case 'X':
		/* Add words from this file to dictionary of excluded words */
		CheckArg;
		wordsource = arg;
		loaddictionary(exclusiondict, wordsource);
		break;

		case 'L':
		/* save language dictionary to file */
		CheckArg;
		savedictionary(languagedict, arg);
		break;

		case 'B':
		/* save bad dictionary to file */
		CheckArg;
		savedictionary(exclusiondict, arg);
		break;

		case 'N':
		/* Set min size of generated gibberish words */
		CheckArg;
		gibberminlen = atol(arg);
		if (gibberminlen < 1)
		  gibberminlen = DEFAULT_GIBBERMINLEN;
		break;

		case 'M':
		/* Set max size of generated gibberish words */
		CheckArg;
		gibbermaxlen = atol(arg);
		if (gibbermaxlen < gibberminlen)
		  gibbermaxlen = DEFAULT_GIBBERMAXLEN;
		break;

		case 'C':
		/* Set count of gibberish words */
		CheckArg;
		gibberlimit = atol(arg);
		if (gibberlimit < 1)
		  gibberlimit = DEFAULT_GIBBERCOUNT;
		/* Create gibberish, adhering to usage rules */
		makegibberdict(gibberlimit, gibberminlen, gibbermaxlen);
		break;

		case 'F':
		/* save gibberish dictionary to file */
		CheckArg;
		worddest = arg;
		savedictionary(gibberdict, worddest);
		break;

		case 'V':
		VERBOSE = TRUE;
		break;

		case 'W':
		VERBOSE2 = TRUE;
		break;

		case 'H':
		usage(argv[0]);

		default:
		usage(argv[0]);
	 }
  }

  if (worddest == NULL)
  {
	printdictionary(gibberdict);
  }
  exit(0);
}

