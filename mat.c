/*
  mat - manipulation of matrices

                                                                c can equal:
  mat_zero                                            0 -> c
  mat_one                                             I -> c
  mat_copy                                            a -> c       a
  mat_add                                         a + b -> c       a or b
  mat_sub                                         a - b -> c       a or b
  mat_mul                                           a b -> c
  mat_mul_tn                                       a' b -> c
  mat_mul_nt                                       a b' -> c
  mat_similarity                                 a b a' -> c       b
  sym_factor               lower triangular factor of a -> c       a
  sym_rdiv      (b already factored)   a / b = a inv(b) -> c       a
  sym_ldiv      (a already factored)   a \ b = inv(a) b -> c       b

  Every matrix parameter is followed by two integers, which give the
  number of rows and the number of columns.  The result is always
  returned in the last matrix.  a' is the transpose of a.  */

#include <assert.h>
#include <math.h>		/* for sqrt() */
#include <stdlib.h>		/* for malloc() and free() */

/* set c to zero */
void mat_zero(void *c, int cr, int cc)
{
  double *_c = (double *)c;
  int i;

  for (i = 0; i < cr*cc; i++)
    _c[i] = 0.;
}

/* set c to the unit matrix */
void mat_one(void *_c, int cr, int cc)
{
  double *c = (double *)_c;
  int i;

  assert(cr == cc);

  mat_zero(c, cr, cc);

#define C(i,j) c[i*cc+j]
  for (i = 0; i < cr; i++)
    C(i,i) = 1.;
#undef C
}

/* copy a to c */
void mat_copy(void *a, int ar, int ac,
	      void *c, int cr, int cc)
{
  double *_a = (double *)a;
  double *_c = (double *)c;
  int i;

  assert(ar == cr && ac == cc);

  for (i = 0; i < ar*ac; i++)
    _c[i] = _a[i];
}

/* Add a and b, and put result in c.  c may be the same as a and/or b. */
void mat_add(void *a, int ar, int ac,
	     void *b, int br, int bc,
	     void *c, int cr, int cc)
{
  double *_a = (double *)a;
  double *_b = (double *)b;
  double *_c = (double *)c;
  int i;

  assert(ar == br && br == cr && ac == bc && bc == cc);

  for (i = 0; i < ar*ac; i++)
    _c[i] = _a[i] + _b[i];
}

/* subtract b from a, and put result in c.  c may be the same as a
   and/or b. */
void mat_sub(void *a, int ar, int ac,
	     void *b, int br, int bc,
	     void *c, int cr, int cc)
{
  double *_a = (double *)a;
  double *_b = (double *)b;
  double *_c = (double *)c;
  int i;

  assert(ar == br && br == cr && ac == bc && bc == cc);

  for (i = 0; i < ar*ac; i++)
    _c[i] = _a[i] - _b[i];
}

/* multiply a by b, put result in c */
void mat_mul(void *a, int ar, int ac,
	     void *b, int br, int bc,
	     void *c, int cr, int cc)
{
  double *_a = (double *)a;
  double *_b = (double *)b;
  double *_c = (double *)c;
  double s;
  int i, j, k;

#define A(i,j) _a[i*ac+j]
#define B(i,j) _b[i*bc+j]
#define C(i,j) _c[i*cc+j]

  assert(ar == cr && ac == br && bc == cc);

  for (i = 0; i < cr; i++)
    for (j = 0; j < cc; j++)
      {
	s = 0.;
	for (k = 0; k < ac; k++)
	  s += A(i,k)*B(k,j);
	C(i,j) = s;
      }
#undef A
#undef B
#undef C
}

/* multiply a' by b, put result in c */
void mat_mul_tn(void *a, int ar, int ac,
		void *b, int br, int bc,
		void *c, int cr, int cc)
{
  double *_a = (double *)a;
  double *_b = (double *)b;
  double *_c = (double *)c;
  double s;
  int i, j, k;

#define A(i,j) _a[i*ac+j]
#define B(i,j) _b[i*bc+j]
#define C(i,j) _c[i*cc+j]

  assert(ac == cr && ar == br && bc == cc);

  for (i = 0; i < cr; i++)
    for (j = 0; j < cc; j++)
      {
	s = 0.;
	for (k = 0; k < ar; k++)
	  s += A(k,i)*B(k,j);
	C(i,j) = s;
      }
#undef A
#undef B
#undef C
}

/* Multiply a by b', put result in c. */
void mat_mul_nt(void *a, int ar, int ac,
		void *b, int br, int bc,
		void *c, int cr, int cc)
{
  double *_a = (double *)a;
  double *_b = (double *)b;
  double *_c = (double *)c;
  double s;
  int i, j, k;

#define A(i,j) _a[i*ac+j]
#define B(i,j) _b[i*bc+j]
#define C(i,j) _c[i*cc+j]

  assert(ar == cr && ac == bc && br == cc);

  for (i = 0; i < cr; i++)
    for (j = 0; j < cc; j++)
      {
	s = 0.;
	for (k = 0; k < ac; k++)
	  s += A(i,k)*B(j,k);
	C(i,j) = s;
      }
#undef A
#undef B
#undef C
}

/* Form the product a*b*a', and leave the result in c.  Return nonzero
   on failure (insufficient memory). */
int mat_similarity(void *a, int ar, int ac,
		   void *b, int br, int bc,
		   void *c, int cr, int cc)
{
  void *t;
  assert(ac==br && br == bc && ar==cr && cr==cc);

  t = malloc(ar*bc*sizeof(double));
  if (!t) return 1;		/* failure */
  mat_mul(a,ar,ac, b,br,bc, t,ar,bc);
  mat_mul_nt(t,ar,bc, a,ar,ac, c,cr,cc);
  free(t);
  return 0;
}

/* Perform Cholesky decomposition on the square, symmetric matrix a,
  and leave the lower triangular factor in c.  The part of c above the
  diagonal is not disturbed.  c may be the same as a.  Returns nonzero
  if a is singular.  Afterwards: if l is the lower triangular part of
  c, and l' is the transpose of l, then a = l l'. */
int sym_factor(void *a, int ar, int ac,
	       void *c, int cr, int cc)
{
  double *_a = (double *)a;
  double *_c = (double *)c;
  double d, s;
  int i, j, k;

#define A(i,j) _a[i*ac+j]
#define C(i,j) _c[i*cc+j]

  assert(ar == ac && ac == cr && cr == cc); /* must be square */

  for (j = 0; j < cc; j++)	/* columns of c */
    {
      s = A(j,j);
      for (i = 0; i < j; i++)	/* rows of c */
	s -= C(j,i)*C(j,i);
      if (s < 0.) return 1;	/* failure (singular matrix) */
      d = C(j,j) = sqrt(s);
      for (i = j+1; i < cc; i++) /* columns of c */
	{
	  s = A(i,j);
	  for (k = 0; k < j; k++)
	    s -= C(j,k)*C(i,k);
	  C(i,j) = s/d;
	}
    }
  return 0;
#undef A
#undef C
}

/* right divide a by b (that is, multiply a by inverse of b), and
   leave the result in c.  b must already have been Cholesky
   decomposed, and only its lower triangle is used.  c may be the same
   as a. */
void sym_rdiv(void *a, int ar, int ac,
	      void *b, int br, int bc,
	      void *c, int cr, int cc)
{
  double *_a = (double *)a;
  double *_b = (double *)b;
  double *_c = (double *)c;
  double s;
  int i, j, k;

#define A(i,j) _a[i*ac+j]
#define B(i,j) _b[i*bc+j]
#define C(i,j) _c[i*cc+j]

  assert(ar == cr && ac == cc && br == bc && ac == br);

  for (i = 0; i < ar; i++)	/* rows of a */
    {
      for (j = 0; j < ac; j++)	/* cols of a */
	{
	  s = A(i,j);
	  for (k = 0; k < j; k++)
	    s -= C(i,k)*B(j,k);
	  C(i,j) = s/B(j,j);
	}
    }

  for (i = 0; i < cr; i++)	/* rows of c */
    {
      for (j = cc; j--; )	/* cols of c */
	{
	  s = C(i,j);
	  for (k = j+1; k < cc; k++)
	    s -= C(i,k)*B(k,j);
	  C(i,j) = s/B(j,j);
	}
    }
#undef A
#undef B
#undef C
}

/* left divide b by a (that is, multiply inverse of a by b), and leave
   the result in c.  a must already have been Cholesky decomposed, and
   only its lower triangle is used.  c may be the same as b. */
void sym_ldiv(void *a, int ar, int ac,
	      void *b, int br, int bc,
	      void *c, int cr, int cc)
{
  double *_a = (double *)a;
  double *_b = (double *)b;
  double *_c = (double *)c;
  double s;
  int i, j, k;

#define A(i,j) _a[i*ac+j]
#define B(i,j) _b[i*bc+j]
#define C(i,j) _c[i*cc+j]

  assert(ar == cr && ac == br && ar == ac && bc == cc);

  for (j = 0; j < cc; j++)	/* columns of c */
    for (i = 0; i < cr; i++)	/* rows of c */
      {
	s = B(i,j);
	for (k = 0; k < i; k++)
	  s -= A(i,k)*C(k,j);
	s = C(i,j) = s/A(i,i);
      }

  for (j = 0; j < cc; j++)	/* columns of c */
    for (i = cr; i--; )		/* rows of c */
      {
	s = C(i,j);
	for (k = i+1; k < cr; k++)
	  s -= A(k,i)*C(k,j);
	C(i,j) = s/A(i,i);
      }
      
#undef A
#undef B
#undef C
}
