/* Copyright (C) 2014  The PARI group.

This file is part of the PARI/GP package.

PARI/GP is free software; you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation. It is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY WHATSOEVER.

Check the License for details. You should have received a copy of it, along
with the package; see the file 'COPYING'. If not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA. */

#include "pari.h"
#include "paripriv.h"


#define dbg_printf0(lvl, fmt) \
  do { if ((lvl) <= DEBUGLEVEL) err_printf(fmt "\n"); } while (0)
#define dbg_printf1(lvl, fmt, a1) \
  do { if ((lvl) <= DEBUGLEVEL) err_printf(fmt "\n", (a1)); } while (0)
#define dbg_printf2(lvl, fmt, a1, a2) \
  do { if ((lvl) <= DEBUGLEVEL) err_printf(fmt "\n", (a1), (a2)); } while (0)
#define dbg_printf3(lvl, fmt, a1, a2, a3) \
  do { if ((lvl) <= DEBUGLEVEL) err_printf(fmt "\n", (a1), (a2), (a3)); } while (0)
#define dbg_printf4(lvl, fmt, a1, a2, a3, a4) \
  do { if ((lvl) <= DEBUGLEVEL) err_printf(fmt  "\n", (a1), (a2), (a3), (a4)); } while (0)
#define dbg_printf dbg_printf1
#define dbg_puts(lvl, str) dbg_printf0(lvl, str)

/**
 * SECTION: Functions dedicated to finding a j-invariant with a given
 * trace.
 */

/* FIXME: This code is shared with
 * torsion_compatible_with_characteristic() in 'torsion.c'. */
static void
hasse_bounds(long *low, long *high, long p)
{
  long two_sqrt_p = usqrt(4*p);
  *low = p + 1 - two_sqrt_p;
  *high = p + 1 + two_sqrt_p;
}


/*
 * a and b must be the result of factoru_pow(), and b must divide a
 * exactly.
 */
INLINE void
famatsmall_divexact(GEN a, GEN b)
{
  long i, j;
  for (i = j = 1; j < lg(gel(a, 1)) && i < lg(gel(b, 1)); ++j)
    if (gel(a, 1)[j] == gel(b, 1)[i])
      gel(a, 2)[j] -= gel(b, 2)[i++];

  for (i = j = 1; j < lg(gel(a, 1)); ++j) {
    if (gel(a, 2)[j]) {
      gel(a, 1)[i] = gel(a, 1)[j];
      gel(a, 2)[i] = gel(a, 2)[j];
      ++i;
    }
  }
  if (i == 1) {
    /* b == a, so a must now be 1. */
    gel(a, 1)[1] = 1;
    gel(a, 2)[1] = 0;
    setlg(gel(a, 1), 2);
    setlg(gel(a, 2), 2);
  } else {
    setlg(gel(a, 1), i);
    setlg(gel(a, 2), i);
  }
}


/*
 * This is Sutherland, 2009, TestCurveOrder.
 *
 * [a4, a6] and p specify an elliptic curve over FF_p.  N is a
 * two-element array containing the two possible curve orders, and n
 * is a two-element array containg the corresponding factorisations as
 * famats.
 */
static long
test_curve_order(
  norm_eqn_t ne, ulong a4, ulong a6,
  long N0, long N1, GEN n0, GEN n1,
  long hasse[2])
{
  pari_sp ltop = avma, av;
  ulong a4t, a6t;
  long m0, m1;
  long hasse_low, hasse_high;
  ulong p = ne->p, pi = ne->pi, T = ne->T;
  ulong swapped = 0;

  if (p <= 11) {
    long card = (long)p + 1 - Fl_elltrace(a4, a6, p);
    return card == N0 || card == N1;
  }

  /* [a4, a6] is the given curve and [a4t, a6t] is its quadratic
   * twist. */
  Fl_elltwist(a4, a6, T, p, &a4t, &a6t);

  m0 = m1 = 1;

  if (N0 + N1 != 2 * (long)p + 2)
    pari_err_BUG("test_curve_order");

  hasse_low = hasse[0];
  hasse_high = hasse[1];

  av = avma;
  for ( ; ; ) {
    GEN pt, Q, tmp;
    long a1, x, n_s;

    pt = random_Flj_pre(a4, a6, p, pi);
    Q = Flj_mulu_pre(pt, m0, a4, p, pi);
    /* FIXME: This is wasteful -- try to work out how to go back to
     * using the e0. */
    tmp = gcopy(n0);
    famatsmall_divexact(tmp, factoru(m0));
    n_s = Flj_order_ufact(Q, N0 / m0, tmp, a4, p, pi);

    if (n_s == 0) {
      /* If m0 divides N1 and m1 divides N0 and N0 < N1,
       * then swap. */
      if ( ! swapped && N1 % m0 == 0 && N0 % m1 == 0) {
        swapspec(n0, n1, N0, N1);
        swapped = 1;
        continue;
      } else {
        avma = ltop;
        return 0;
      }
    }

    m0 *= n_s;
    a1 = (2 * p + 2) % m1;
    /* Using ceil(n/d) = (n + d - 1)/d */
    x = (hasse_low + m0 - 1) / m0;
    x *= m0;
    for ( ; x <= hasse_high; x += m0) {
      if ((x % m1) == a1 && x != N0 && x != N1)
        break;
    }
    /* We exited the loop because we finished iterating, not because
     * of the break.  That means every x in N was either N0 or N1, so
     * we return true. */
    if (x > hasse_high) {
      avma = ltop;
      return 1;
    }

    lswap(a4, a4t);
    lswap(a6, a6t);
    lswap(m0, m1);
    avma = av;
  }
}

INLINE int
jac_eq_or_opp(GEN P, GEN Q, ulong p, ulong pi)
{
  /* (X1:Y1:Z1) and (X2:Y2:Z2) in Jacobian coordinates are equal
   * or opposite iff X1 Z2^2 = X2 Z1^2. */
  return ! Fl_sub(Fl_mul_pre(P[1], Fl_sqr_pre(Q[3], p, pi), p, pi),
                  Fl_mul_pre(Q[1], Fl_sqr_pre(P[3], p, pi), p, pi), p);
}


/* This is Sutherland 2009 Algorithm 1.1 */
static long
find_j_inv_with_given_trace(
  ulong *j_t, norm_eqn_t ne, long rho_inv, long max_curves)
{
  pari_sp ltop = avma, av;
  long curves_tested = 0, batch_size;
  long N0, N1, hasse[2];
  GEN n0, n1;
  long i, found = 0;
  ulong p = ne->p, pi = ne->pi;
  long t = ne->t;
  ulong p1 = p + 1, j = 0, m, c_1728 = 1728 % p;
  GEN A4, A6, tx, ty;
  /* This number must be the same as LAST_X1_LEVEL in 'torsion.c', */
  enum { MAX_X1_CURVE_LVL = 39 };

  /* ellap(ellinit(ellfromj(Mod(1,2)))) == -1
   * ellap(ellinit(ellfromj(Mod(1,3)))) ==  1
   * ellap(ellinit(ellfromj(Mod(2,3)))) ==  2 */
  if (p == 2 || p == 3) {
    if (t == 0)
      pari_err_BUG("find_j_inv_with_given_trace");
    *j_t = t;
    return 1;
  }

  N0 = (long)p1 - t;
  N1 = (long)p1 + t;
  n0 = factoru(N0);
  n1 = factoru(N1);

  /* FIXME: Select m more intelligently.  Currently just the biggest
   * common divisor of N0 and N1 less than 39. */
  m = cgcd(N0, N1);
  av = avma;
  if (m > MAX_X1_CURVE_LVL) {
    GEN factm = factoru(m);
    long nfactors = lg(gel(factm, 1)) - 1;
    for (i = 1; i <= nfactors; ) {
      m /= gel(factm, 1)[i];
      if (m <= MAX_X1_CURVE_LVL)
        break;
      gel(factm, 2)[i] -= 1;
      if (gel(factm, 2)[i] == 0)
        ++i;
    }
    avma = av;
  }

  /* Select batch size so that we have roughly a 50% chance of finding
   * a good curve in a batch. */
  batch_size = 1.0 + rho_inv / (2.0 * m);
  A4 = cgetg(batch_size + 1, t_VECSMALL);
  A6 = cgetg(batch_size + 1, t_VECSMALL);
  tx = cgetg(batch_size + 1, t_VECSMALL);
  ty = cgetg(batch_size + 1, t_VECSMALL);

  if (DEBUGLEVEL > 1) {
    err_printf("  Selected torsion constraint m = %lu and batch "
               "size = %ld\n", m, batch_size);
  }

  hasse_bounds(&hasse[0], &hasse[1], p);

  av = avma;
  while ( ! found && (max_curves <= 0 || curves_tested < max_curves)) {
    random_curves_with_m_torsion((ulong *)(A4 + 1), (ulong *)(A6 + 1),
                                 (ulong *)(tx + 1), (ulong *)(ty + 1),
                                 batch_size, m, p);
    for (i = 1; i <= batch_size; ++i) {
      ulong a4, a6;
      GEN P, p1P, tP;
      ++curves_tested;
      a4 = A4[i];
      a6 = A6[i];
      j = Fl_ellj_pre(a4, a6, p, pi);
      if (j == 0 || j == c_1728)
        continue;

      P = random_Flj_pre(a4, a6, p, pi);
      p1P = Flj_mulu_pre(P, p1, a4, p, pi);
      tP = Flj_mulu_pre(P, t, a4, p, pi);

      if (jac_eq_or_opp(p1P, tP, p, pi)
          && test_curve_order(ne, a4, a6, N0, N1, n0, n1, hasse)) {
        found = 1;
        break;
      }
      avma = av;
    }
  }
  *j_t = j;
  avma = ltop;
  return curves_tested;
}


/**
 * SECTION: Functions for dealing with polycyclic presentations.
 */

/* A polycyclic presentation P is a GEN with the following structure:
 *
 * gel(P, 1): t_VECSMALL of generator norms
 * gel(P, 2): t_VECSMALL of relative orders
 * gel(P, 3): t_VECSMALL of power relations
 * gel(P, 4): t_VEC of all QFIs
 */

#define PCP_GEN_NORMS(P) gel((P), 1)
#define PCP_REL_ORDERS(P) gel((P), 2)
#define PCP_POW_RELS(P) gel((P), 3)
#define PCP_QFI_TABLE(P) gel((P), 4)
#define PCP_NGENS(P) (lg(PCP_GEN_NORMS(P)) - 1)

INLINE ulong
pcp_order(GEN P) { return zv_prod(PCP_REL_ORDERS(P)); }

static int
pcp_cmpf(GEN x, GEN y)
{
  pari_sp ltop = avma;
  int eq = gequal(redimag(x), redimag(y));
  avma = ltop;
  return eq;
}


static long
linear_search(GEN vec, GEN elt, int (*cmpf)(GEN, GEN))
{
  long len = lg(vec);
  long idx = 1;

  if (cmpf == NULL)
    cmpf = gequal;

  for ( ; idx < len; ++idx)
    if (cmpf(gel(vec, idx), elt))
      break;

  return idx == len ? 0 : idx;
}


static GEN
next_prime_generator(GEN DD, long D, ulong u, ulong *p)
{
  pari_sp av = avma, av1;
  GEN gen, genred;
  long norm;
  while (1) {
    *p = unextprime(*p + 1);
    if (kross(D, (long)*p) != -1 && u % *p != 0) {
      gen = primeform_u(DD, *p);
      av1 = avma;

      /* If the reduction of gen has norm = 1, then it is the identity
       * form and is therefore skipped. */
      genred = redimag(gen);
      norm = itos(gel(genred, 1));
      avma = av1;
      if (norm != 1)
        break;
      avma = av;
    }
  }
  return gen;
}


/*
 * This is Sutherland 2009, Algorithm 2.2 (p16).
 *
 * FIXME: Switch order of h and D?
 */
static GEN
minimal_polycyclic_presentation(ulong h, long D, ulong u)
{
  pari_sp ltop = avma, av, btop;
  GEN T, DD, ident;
  ulong curr_p = 1, nelts = 1;
  GEN pcp;

  DD = stoi(D);
  pcp = cgetg(5, t_VEC);

  /* Trivial presentation */
  gel(pcp, 1) = cgetg(1, t_VECSMALL);
  gel(pcp, 2) = cgetg(1, t_VECSMALL);
  gel(pcp, 3) = cgetg(1, t_VECSMALL);
  /* FIXME: T should be a hash table of some kind so that we can
   * avoid the linear searches. */
  T = vectrunc_init(h + 1);
  av = avma;
  ident = gerepileupto(av, redimag(primeform_u(DD, 1)));
  vectrunc_append(T, ident); /* qfi_ident_from_disc(D) */
  gel(pcp, 4) = T;

  if (h == 1)
    return gerepileupto(ltop, pcp);

  btop = avma;
  while (nelts < h) {
    GEN gamma_i = next_prime_generator(DD, D, u, &curr_p);
    GEN beta = gamma_i;
    ulong N = glength(T), ri = 1, si;

    /* FIXME: The linear search will *recalculate* redimag(beta) for
     * every element of T, completely unnecessarily. */
    while ((si = linear_search(T, beta, pcp_cmpf)) == 0) {
      ulong j;
      for (j = 1; j <= N; ++j)
        vectrunc_append(T, gmul(beta, gel(T, j)));
      beta = gmul(beta, gamma_i);
      ++ri;
    }
    if (ri > 1) {
      long j;
      PCP_GEN_NORMS(pcp) = vecsmall_append(PCP_GEN_NORMS(pcp), curr_p);
      PCP_REL_ORDERS(pcp) = vecsmall_append(PCP_REL_ORDERS(pcp), ri);
      nelts *= ri;

      N = 1;
      /* FIXME: This decrement is just because we need indices
       * starting at 0 for the formula in the for loop to work. */
      si--;
      for (j = 1; j < lg(PCP_REL_ORDERS(pcp)); ++j) {
        ulong rj = PCP_REL_ORDERS(pcp)[j];
        PCP_POW_RELS(pcp) = vecsmall_append(PCP_POW_RELS(pcp), (si / N) % rj);
        N *= rj;
      }
    }

    if (gc_needed(btop, 2))
      gerepileall(btop, 1, &pcp);
  }
  return gerepilecopy(ltop, pcp);
}

INLINE ulong
classno_wrapper(long D)
{
  pari_sp av = avma;
  GEN clsgp;
  ulong h;
  clsgp = quadclassunit0(stoi(D), 0, 0, DEFAULTPREC);
  h = itou(gel(clsgp, 1));
  avma = av;
  return h;
}


/**
 * SECTION: Functions for calculating class polynomials.
 */

#define HALFLOGPI 0.57236494292470008707171367567653

/*
 * Based on logfac() in Sutherland's classpoly package.
 *
 * Ramanujan approximation to log(n!), accurate to O(1/n^3)
 */
INLINE double
logfac(long n)
{
  return n * log(n) - (double) n +
    log((double) n * (1.0 + 4.0 * n * (1.0 + 2.0 * n))) / 6.0 +
    HALFLOGPI;
}


#define LOG2E 1.44269504088896340735992468100189


/* This is based on Sutherland 2009, Lemma 8 (p31). */
static double
upper_bound_on_classpoly_coeffs(long D, GEN pcp)
{
  pari_sp ltop = avma;
  ulong h = pcp_order(pcp);
  GEN C = dbltor(2114.567);
  double Mk, m, logbinom;
  GEN tmp = mulrr(mppi(LOWDEFAULTPREC), sqrtr(stor(-D, LOWDEFAULTPREC)));
  /* We treat this case separately since the table is not initialised
   * when h = 1. This is the same as in the for loop below but with ak
   * = 1. */
  double log2Mk = dbllog2r(mpadd(mpexp(tmp), C));
  double res = log2Mk;
  ulong maxak = 1;
  double log2Mh = log2Mk;

  pari_sp btop = avma;
  ulong k;
  for (k = 2; k <= h; ++k) {
    /* FIXME: Would be better if the table was a list of vecsmalls to
     * avoid the conversion here. */
    ulong ak = itou(gmael(PCP_QFI_TABLE(pcp), k, 1));
    /* Unfortunately exp(tmp/a[k]) can overflow for even moderate
     * discriminants, so we need to do this calculation with t_REALs
     * instead of just doubles.  Sutherland has a (much more
     * complicated) implementation in the classpoly package which
     * should be consulted if this ever turns out to be a bottleneck.
     *
     * [Note that one idea to avoid t_REALs is the following: we have
     * log(e^x + C) - x <= log(2) ~ 0.69 for x >= log(C) ~ 0.44 and
     * the difference is basically zero for x slightly bigger than
     * log(C).  Hence for large discriminants, we will always have x =
     * \pi\sqrt{-D}/ak >> log(C) and so we could approximate log(e^x +
     * C) by x.] */
    log2Mk = dbllog2r(mpadd(mpexp(divru(tmp, ak)), C));
    res += log2Mk;
    if (ak > maxak) {
      maxak = ak;
      log2Mh = log2Mk;
    }
    avma = btop;
  }

  Mk = pow(2.0, log2Mh);
  m = floor((h + 1)/(Mk + 1.0));
  /* This line computes "log2(itos(binomialuu(h, m)))".  The smallest
   * fundamental discriminant for which logbinom is not zero is
   * -1579751. */
  logbinom = (m > 0 && m < h)
    ? LOG2E * (logfac(h) - logfac(m) - logfac(h - m))
    : 0;
  avma = ltop;
  return res + logbinom - m * log2Mh + 2.0;
}


/* NB: Sutherland defines V_MAX to be 1200 with saying why. */
#define V_MAX 1200

#define NSMALL_PRIMES 11
static const long small_primes[11] = {
    2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31
};

static long
is_smooth_enough(ulong *factors, long v)
{
  long i;
  *factors = 0;
  for (i = 0; i < NSMALL_PRIMES; ++i) {
    long p = small_primes[i];
    if (v % p == 0)
      *factors |= 1 << i;
    while (v % p == 0)
      v /= p;
    if (v == 1)
      break;
  }
  return v == 1;
}


/* Hurwitz class number of |D| assuming hclassno() and associated
 * conversion to double costs much more than unegisfundamental(). */
INLINE double
hclassno_wrapper(long D, GEN pcp)
{
  /* FIXME: Can probably calculate hurwitz faster using -D, factor(u)
   * and classno(D). */
  pari_sp av = avma;
  ulong abs_D = D < 0 ? -D : D;
  double hurwitz;

  if (pcp && unegisfundamental(abs_D))
    hurwitz = (double) pcp_order(pcp);
  else
    hurwitz = rtodbl(gtofp(hclassno(utoi(abs_D)), DEFAULTPREC));
  avma = av;
  return hurwitz;
}


/*
 * This is Sutherland 2009, Algorithm 2.1 (p8).
 *
 * NB: This function is not gerepileupto-safe.
 */
static GEN
select_classpoly_prime_pool(
  long D, double min_prime_bits, double delta, GEN pcp)
{
  pari_sp av;
  double prime_bits = 0.0, hurwitz, z;
  ulong i;
  /* t_min[v] will hold the lower bound of the t we need to look at
   * for a given v. */
  ulong t_min[V_MAX], t_size_lim;
  GEN res;

  if (delta <= 0)
    pari_err_BUG("select_suitable_primes");
  hurwitz = hclassno_wrapper(D, pcp);

  res = cgetg(1, t_VEC);
  /* Initialise t_min to be all 2's.  This avoids trace 0 and trace
   * 1 curves. */
  for (i = 0; i < V_MAX; ++i)
    t_min[i] = 2;

  /* maximum possible trace = sqrt(2^BIL - D) */
  t_size_lim = 2.0 * sqrt((1UL << (BITS_IN_LONG - 2)) - (((ulong)-D) >> 2));

  av = avma;
  for (z = -D / (2.0 * hurwitz); ; z *= delta + 1.0) {
    /* v_bound_aux = -4 z H(-D). */
    double v_bound_aux = -4.0 * z * hurwitz;
    ulong v;
    dbg_printf(1, "z = %.2f", z);
    for (v = 1; ; ++v) {
      ulong pcount = 0, t, t_max, factors;
      ulong m_vsqr_D = v * v * (ulong)(-D);
      /* hurwitz_ratio_bound = 11 * log(log(v + 4))^2 */
      double hurwitz_ratio_bound = log(log(v + 4.0)), max_p, H;
      hurwitz_ratio_bound *= 11.0 * hurwitz_ratio_bound;

      if (v >= v_bound_aux * hurwitz_ratio_bound / D || v >= V_MAX)
        break;

      if ( ! is_smooth_enough(&factors, v))
        continue;
      H = hclassno_wrapper(m_vsqr_D, 0);

      /* t <= 2 sqrt(p) and p <= z H(-v^2 D) and
       *
       *   H(-v^2 D) < vH(-D) (11 log(log(v + 4))^2)
       *
       * This last term is v * hurwitz * hurwitz_ratio_bound. */

      max_p = z * v * hurwitz * hurwitz_ratio_bound;
      t_max = 2.0 * mindd(sqrt((1UL << (BITS_IN_LONG - 2)) - (m_vsqr_D >> 2)),
                          sqrt(max_p));
      for (t = t_min[v]; t <= t_max; ++t) {
        ulong possible_4p = t * t + m_vsqr_D;
        if (possible_4p % 4 == 0) {
          ulong possible_p = possible_4p / 4;
          if (uisprime(possible_p)) {
            long p = possible_p;
            double rho_inv;
            GEN hit;

            rho_inv = p / H;
            hit = mkvecsmall4(p, t, v, (long)rho_inv);
            /* FIXME: Avoid doing GC for every prime as here. */
            res = gerepileupto(av, concat(res, hit));
            prime_bits += log2(p);
            ++pcount;
          }
        }
      }
      t_min[v] = t_max + 1;

      if (pcount) {
        dbg_printf2(2, "  Found %lu primes for v = %lu.", pcount, v);
        if (gc_needed(av, 2))
          res = gerepilecopy(av, res);
      }
      if (prime_bits > min_prime_bits) {
        dbg_printf2(1, "Found %ld primes; total size %.2f bits.",
                    glength(res), prime_bits);
        return gerepilecopy(av, res);
      }
    }

    /* Have we exhausted all possible solutions that fit in machine words? */
    if (t_min[1] >= t_size_lim) {
      char *err = stack_sprintf("class polynomial of discriminant %ld", D);
      pari_err(e_ARCH, err);
    }
  }
}


static int
primecmp(void *data, GEN v1, GEN v2)
{
  (void)data;
  return v1[4] - v2[4];
}


static GEN
select_classpoly_primes(
  long D, long k, double delta, GEN pcp)
{
  pari_sp av = avma;
  long i;
  double prime_bits, min_prime_bits, b;
  GEN prime_pool;

  if (k < 2)
    pari_err_BUG("select_suitable_primes");

  b = upper_bound_on_classpoly_coeffs(D, pcp);
  dbg_printf(1, "b = %.2f", b);
  min_prime_bits = k * b;

  prime_pool = select_classpoly_prime_pool(D, min_prime_bits, delta, pcp);

  /* FIXME: Apply torsion constraints */
  /* FIXME: Rank elts of res according to cost/benefit ratio */
  prime_pool = gen_sort(prime_pool, 0, primecmp);

  prime_bits = 0.0;
  for (i = 1; i < lg(prime_pool); ++i) {
    prime_bits += log2(gel(prime_pool, i)[1]);
    if (prime_bits > b)
      break;
  }
  dbg_printf(1, "Selected %ld primes.", i);
  return gerepilecopy(av, vecslice0(prime_pool, 1, i));
}


/*
 * This is Sutherland 2009 Algorithm 1.2.
 */
static long
oneroot_of_classpoly(ulong *j_endo, ulong j, norm_eqn_t ne, GEN *mpdb)
{
  /* FIXME: Return u_compl as an indication as to whether we know that
   * *j_endo is correct, rather than just probably correct. */

  long nfactors, L_bound, i;
  ulong p = ne->p, pi = ne->pi;
  GEN factors, u_levels, vdepths;

  if (j == 0 || j == 1728 % p)
    pari_err_BUG("oneroot_of_classpoly");
  if (ne->w == 1) {
    *j_endo = j;
    return 1;
  }

  factors = gel(ne->factw, 1);
  u_levels = ne->u_levels;
  vdepths = ne->vdepths;
  nfactors = lg(factors) - 1;
  L_bound = maxdd(log(-ne->D), (double)ne->v);

  /* Iterate over the primes L dividing w */
  for (i = 1; i <= nfactors; ++i) {
    pari_sp av = avma;
    GEN phi;
    long jlvl, lvl_diff, depth = vdepths[i];
    long L = factors[i];
    if (L > L_bound)
      break;

    phi = modpoly_db_getp(mpdb, L, p);

    /* FIXME: Can I reuse paths created in j_level_in_volcano() later
     * in {ascend,descend}_volcano()? */
    jlvl = j_level_in_volcano(phi, j, p, pi, L, depth);
    lvl_diff = u_levels[i] - jlvl;

    /*
    while (vLu--)
      u_compl /= L;
    */

    if (lvl_diff < 0) {
      /* j's level is less than v(u) so we must ascend */
      j = ascend_volcano(phi, j, p, pi, jlvl, L, depth, -lvl_diff);
    } else if (lvl_diff > 0) {
      /* Otherwise j's level is greater than v(u) so we descend */
      j = descend_volcano(phi, j, p, pi, jlvl, L, depth, lvl_diff);
    }
    avma = av;
  }
  /* At this point the probability that j has the wrong endomorphism
   * ring is about \sum_{p|u_compl} 1/p (and u_compl must be bigger
   * than L_bound, so pretty big), so just return it and rely on
   * detection code in enum_j_with_endo_ring().  Detection is that we
   * hit a previously found j-invariant earlier than expected.  OR, we
   * evaluate class polynomials of the suborders at j and if any are
   * zero then j must be chosen again.  */
  *j_endo = j;
  return j != 0 && j != 1728 % p;
}


/*
 * This is Sutherland 2009 Algorithm 1.3 (p14).
 */
static long
enum_j_with_endo_ring_small_disc_r(
  ulong *res, long scale, long offset, norm_eqn_t ne,
  GEN *mpdb, GEN pcp, long k)
{
  long L = PCP_GEN_NORMS(pcp)[k];
  long r = PCP_REL_ORDERS(pcp)[k];
  long d = z_lval(ne->w, L); /* volcano_depth(L, u, v); */
  long i, j_idx;
  ulong p = ne->p, pi = ne->pi;

  pari_sp av = avma;
  long plen;
  GEN phi = modpoly_db_getp(mpdb, L, p);
  GEN jpath_g = cgetg(d + r + 1, t_VECSMALL);
  ulong *jpath = (ulong *) &jpath_g[1];
  jpath[0] = res[offset];
  plen = walk_surface_path(jpath, phi, p, pi, L, d, r - 1);
  if (plen != r - 1) {
    avma = av;
    return 0;
  }

  scale /= r;

  /* j_idx = scale * i + offset */
  for (i = 1, j_idx = scale + offset; i < r; ++i, j_idx += scale)
    res[j_idx] = jpath[i];

  avma = av;

  if (k > 1) {
    --k;
    /* j_idx = scale * i + offset */
    for (i = 0, j_idx = offset; i < r; ++i, j_idx += scale) {
      long ok = enum_j_with_endo_ring_small_disc_r(
        res, scale, j_idx, ne, mpdb, pcp, k);
      if ( ! ok)
        return 0;
    }
  }
  return 1;
}


INLINE GEN
enum_j_with_endo_ring_small_disc(
  ulong j0, norm_eqn_t ne, GEN *mpdb, GEN pcp, long max_elts)
{
  pari_sp av = avma;
  long ngens = PCP_NGENS(pcp);
  GEN res = cgetg(max_elts + 1, t_VECSMALL);
  res[1] = j0;
  if (ngens > 0) {
    long ok = enum_j_with_endo_ring_small_disc_r(
      (ulong *)&res[1], max_elts, 0, ne, mpdb, pcp, ngens);
    if ( ! ok) {
      avma = av;
      return NULL;
    }
  }
  return res;
}


/*
 * Given an Flx linpol, assumed to be linear, return its unique root
 * in Fl.
 */
static long
unique_root(ulong *rt, GEN linpol, ulong p)
{
  ulong t;
  if (lg(linpol) > 4) {
    /* linpol is not linear.  Should only happen when |D| is smaller
     * than 4*L1*L2 where L1 and L2 are the prime norms of a generator
     * of H(D). */
    return 0;
  }
  t = Fl_neg(linpol[2], p);
  if (linpol[3] == 1) {
    *rt = t;
    return 1;
  }
  *rt = Fl_div(t, linpol[3], p);
  return 1;
}


/* FIXME: This comment is old; it should be updated. */
/*
 * Assumes M is an nrows x ncols matrix (given as an array of length
 * nrows * ncols) whose first row is an L-thread of j-invariants and
 * the first column is an LL-thread of j-invariants.  This function
 * fills the (nrows-1) x (ncols-1) submatrix following Sutherland,
 * 2012, Section 2.3.  That is, the (i,j) entry is the unique root of
 * the gcd of
 *
 *   Phi_L(X, M[i, j - 1])  and  Phi_LL(X, M[i - 1, j]).
 */
static long
fill_parallel_path(ulong box[], ulong p, ulong pi,
                   GEN phi_row, GEN phi_col,
                   ulong L_col, ulong L_row,
                   ulong direction_scale, ulong thread_scale,
                   ulong idx, ulong end_idx)
{
  pari_sp ltop = avma;
  long ok;
  ulong i;
  for (i = idx + thread_scale; i < end_idx; i += thread_scale) {
    ulong left_j = box[i - direction_scale];
    ulong up_j   = box[i - thread_scale];
    GEN modpol_left_j = Flm_Fl_modpoly_evalx(phi_row, L_col, left_j, p, pi);
    GEN modpol_up_j = Flm_Fl_modpoly_evalx(phi_col, L_row, up_j, p, pi);
    GEN next_j_pol = Flx_gcd(modpol_left_j, modpol_up_j, p);
    ok = unique_root(box + i, next_j_pol, p);
    avma = ltop;
    if ( ! ok)
      return 0;
  }
  return 1;
}


static long
fill_box(
  ulong box[], norm_eqn_t ne, GEN *mpdb, GEN pcp, ulong max_elts)
{
  pari_sp av = avma;
  ulong ngens = PCP_NGENS(pcp);
  long *gen_norms = &PCP_GEN_NORMS(pcp)[1];
  long *rel_ords = &PCP_REL_ORDERS(pcp)[1];
  ulong *m = (ulong *) new_chunk(ngens);
  ulong i, p = ne->p, pi = ne->pi;
  long w = ne->w, k, plen;
  m[0] = 1;
  for (i = 1; i < ngens; ++i)
    m[i] = m[i - 1] * rel_ords[i - 1];

  for (k = ngens - 1; k >= 0; --k) {
    ulong L = gen_norms[k];
    ulong r = rel_ords[k];
    /* FIXME: We should already know d from an earlier calculation. */
    ulong d = u_lval(w, L); /* volcano_depth(L, u, v); */
    ulong g;
    GEN phi_L = modpoly_db_getp(mpdb, L, p);
    pari_sp av2 = avma;

    ulong *jpath = (ulong *) new_chunk(d + r);
    jpath[0] = box[0];
    plen = walk_surface_path(jpath, phi_L, p, pi, L, d, r - 1);
    if (plen != (long)r - 1) {
      avma = av;
      return 0;
    }

    /* Copy L-thread into box. */
    for (i = 1; i < r; ++i)
      box[i * m[k]] = jpath[i];
    avma = av2;

    /* Extend each existing element of the box (except box[0], hence
     * start with i = 1 instead of i = 0) in parallel with the new
     * thread. */
    for (g = ngens - 1; g > (ulong)k; --g) {
      ulong LL = gen_norms[g];
      GEN phi_LL = modpoly_db_getp(mpdb, LL, p);
      for (i = m[g]; i < max_elts; i += m[g]) {
        if ((g == ngens - 1) || (i % m[g + 1] != 0)) {
          if ( ! fill_parallel_path(box, p, pi, phi_LL, phi_L,
                                    LL, L, m[g], m[k], i, m[k] * r + i)) {
            avma = av;
            return 0;
          }
        }
      }
      avma = av2;
    }
  }
  avma = av;
  return 1;
}


GEN
enum_j_with_endo_ring(
  ulong j0, norm_eqn_t ne, GEN *mpdb, GEN pcp, long max_elts)
{
  /* res is a matrix (or rather, a *volume*) for which the (j0, j2,
   * .., jk)th entry is located at j0 * R1k + j1 * R2k + ... + jk
   * where, if (r0, r2, .., rk) are the relative orders of the
   * polycyclic presentation, Rik denotes the product of ri, .., rk. */
  pari_sp av = avma;
  GEN gen_norms = PCP_GEN_NORMS(pcp);
  long ngens = PCP_NGENS(pcp);
  GEN res = cgetg(max_elts + 1, t_VECSMALL);
  res[1] = j0;

  if (ngens == 0)
    return res;

  if (ngens > 1) {
    /* The norms of the last two generators. */
    long L1 = gen_norms[ngens];
    long L2 = gen_norms[ngens - 1];
    /* If 4 L1^2 L2^2 > |D|, */
    if (4 * L1 * L1 * L2 * L2 > -ne->D)
      return enum_j_with_endo_ring_small_disc(j0, ne, mpdb, pcp, max_elts);
  }

  if (fill_box((ulong *)&res[1], ne, mpdb, pcp, max_elts))
    return res;
  avma = av;
  return NULL;
}


INLINE ulong
select_twisting_param(ulong p)
{
  ulong T;
  do
    T = random_Fl(p);
  while (krouu(T, p) != -1);
  return T;
}


INLINE void
setup_norm_eqn(norm_eqn_t ne, long D, long u, GEN norm_eqn)
{
  GEN factw;
  long i, nfactors;

  ne->D = D;
  ne->u = u;
  ne->t = norm_eqn[2];
  ne->v = norm_eqn[3];
  ne->w = u * ne->v;
  ne->p = (ulong) norm_eqn[1];
  ne->pi = get_Fl_red(ne->p);
  ne->T = select_twisting_param(ne->p);
  ne->factw = factw = factoru(u * ne->v);
  nfactors = lg(gel(factw, 1)) - 1;
  ne->u_levels = cgetg(nfactors + 1, t_VECSMALL);
  for (i = 1; i <= nfactors; ++i)
    ne->u_levels[i] = z_lval(u, gel(factw, 1)[i]);
  ne->vdepths = gel(factw, 2);
}


static int
classpoly0(
  ulong *total_curves_tested,
  GEN norm_eqn, GEN *hilb, GEN *P, long D, long u, GEN pcp, GEN *mpdb,
  long xvar)
{
  pari_sp av = avma;
  ulong j_t = 0, j_endo = 0;
  long found_j_endo, trace_tries = 0, endo_tries = 0, rho_inv = norm_eqn[4];
  int stab;
  GEN res, pol;
  norm_eqn_t ne;

  setup_norm_eqn(ne, D, u, norm_eqn);
  dbg_printf3(2, "p = %ld, t = %ld, v = %ld", ne->p, ne->t, ne->v);

  do {
    do {
      /* FIXME: Set batch size according to expected number of tries and
       * experimental cost/benefit analysis. */
      trace_tries = find_j_inv_with_given_trace(&j_t, ne, rho_inv, 0);
      if (j_t == 0) {
        pari_err_BUG("classpoly0: "
                     "Couldn't find j-invariant with given trace.");
      }
      dbg_printf4(2, "  j-invariant %ld has trace +/-%ld (%ld tries, 1/rho = %ld)",
                  j_t, ne->t, trace_tries, rho_inv);
      *total_curves_tested += trace_tries;

      found_j_endo = oneroot_of_classpoly(&j_endo, j_t, ne, mpdb);
      if ( ! found_j_endo) {
        dbg_printf(1, "Couldn't find j-invariant isogenous to %lu with "
                   "given endo ring; trying again...", j_t);
      }
      ++endo_tries;
    } while ( ! found_j_endo);

    res = enum_j_with_endo_ring(j_endo, ne, mpdb, pcp, pcp_order(pcp));
  } while ( ! res);

  dbg_printf2(2, "  j-invariant %ld has correct endomorphism ring "
              "(%ld tries)", j_endo, endo_tries);
  dbg_printf(3, "  all such j-invariants: %Ps", res);

  pol = gerepileupto(av, Flv_roots_to_pol(res, ne->p, xvar));
  dbg_printf2(3, "  Hilbert polynomial mod %ld: %Ps", ne->p, pol);

  stab = ZX_incremental_CRT(hilb, pol, P, ne->p);
  return stab;
}

GEN
polclass(GEN DD, long xvar)
{
  pari_sp ltop = avma, btop;
  long D, dummy;
  GEN prime_lst;
  ulong total_curves_tested = 0;
  long nprimes, i;
  GEN hilb = 0;  /* The Hilbert class polynomial */
  GEN P; /* P is the product of all the p */
  ulong u, classno;
  GEN pcp, mpdb;
  static const long k = 2;
  static const double delta = 0.5;

  if (xvar < 0)
    xvar = 0;
  check_quaddisc_imag(DD, &dummy, "polclass");

  D = itos(DD);
  if (D == -3) /* x */
    return pol_x(xvar);
  if (D == -4) /* x - 1728 */
    return gerepileupto(ltop, deg1pol(gen_1, stoi(-1728), xvar));

  (void) corediscs(D, &u);
  classno = classno_wrapper(D);

  dbg_printf2(1, "D = %ld, conductor = %ld", D, u);

  pcp = minimal_polycyclic_presentation(classno, D, u);

  prime_lst = select_classpoly_primes(D, k, delta, pcp);
  mpdb = modpoly_db_init(0);

  hilb = ZX_init_CRT(pol_0(xvar), 1L, xvar);
  P = gen_1;

  nprimes = lg(prime_lst) - 1;
  btop = avma;
  for (i = 1; i <= nprimes; ++i) {
    (void) classpoly0(&total_curves_tested, gel(prime_lst, i),
                      &hilb, &P, D, u, pcp, &mpdb, xvar);
    if (gc_needed(btop, 2))
      gerepileall(btop, 2, &hilb, &P);
#if 0
    if (quit_on_first_stabilisation && stab)
      break;
#endif
  }

  dbg_printf(1, "Total number of curves tested: %lu",
             total_curves_tested);

  modpoly_db_clear(mpdb);
  /* According to Sutherland, 2009, Section 6.3, the coefficients of
   * the class polynomial lie in the interval (-P/2, P/2) when the
   * modulus P with respect to which we are computing the class
   * polynomial is greater than the product of all the small primes.
   * This is called the "centred representative" and is precisely the
   * value maintained by ZX_incremental_CRT().  We can therefore
   * return hilb directly. */
  return gerepilecopy(ltop, hilb);
}