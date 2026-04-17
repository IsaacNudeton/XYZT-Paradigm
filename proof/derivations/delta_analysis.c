/*
 * delta_correction.c — The open problem.
 *
 * δ = 2/9 is close. The exact δ from measured masses is slightly off.
 * b₀ = 6π is close. The exact b₀ is 3.8% above.
 *
 * Are these the same gap?
 */

#include <stdio.h>
#include <math.h>

int main(void)
{
    double me = 0.51099895, mmu = 105.6583755, mtau = 1776.86;
    double mp = 938.272088;
    double alpha = 7.2973525693e-3;

    /* Koide parameters from measured masses */
    double sme = sqrt(me), smmu = sqrt(mmu), smtau = sqrt(mtau);
    double M = (sme + smmu + smtau) / 3.0;
    double A = sqrt(2.0);

    /* Extract exact delta from tau (most stable measurement) */
    double cos_tau = (smtau/M - 1.0) / A;
    double delta_exact = acos(cos_tau);  /* i=0 → tau */

    double delta_approx = 2.0/9.0;
    double delta_gap = delta_exact - delta_approx;
    double delta_gap_frac = delta_gap / delta_exact;

    printf("THE GAP\n\n");
    printf("  delta_exact  = %.10f rad\n", delta_exact);
    printf("  delta = 2/9  = %.10f rad\n", delta_approx);
    printf("  gap          = %.6e rad\n", delta_gap);
    printf("  fractional   = %.6f%%\n\n", delta_gap_frac * 100);

    /* b₀ gap */
    double mpl = 2.176434e-8;  /* kg */
    double mp_kg = 1.672623e-27;
    double ratio = mp_kg / mpl;
    double b0_exact = -2.0 * M_PI / (alpha * log(ratio));
    double b0_approx = 6.0 * M_PI;
    double b0_gap_frac = (b0_exact - b0_approx) / b0_exact;

    printf("  b0_exact     = %.8f\n", b0_exact);
    printf("  b0 = 6pi     = %.8f\n", b0_approx);
    printf("  fractional   = %.6f%%\n\n", b0_gap_frac * 100);

    printf("  delta gap: %.4f%%\n", delta_gap_frac * 100);
    printf("  b0 gap:    %.4f%%\n", b0_gap_frac * 100);
    printf("  Ratio of gaps: %.4f\n\n", delta_gap_frac / b0_gap_frac);

    /* Are they related by a simple factor? */
    printf("RELATIONSHIP:\n\n");
    printf("  delta_gap / b0_gap = %.6f\n", delta_gap_frac / b0_gap_frac);
    printf("  alpha              = %.6f\n", alpha);
    printf("  gap_ratio / alpha  = %.4f\n", (delta_gap_frac/b0_gap_frac)/alpha);
    printf("  gap_ratio * 137    = %.4f\n", (delta_gap_frac/b0_gap_frac)/alpha);

    /* What if delta_exact = 2/9 + correction proportional to alpha? */
    printf("\n\nCORRECTION ANSATZ: delta = 2/9 + c*alpha\n\n");
    double c = delta_gap / alpha;
    printf("  c = (delta_exact - 2/9) / alpha = %.6f\n", c);
    printf("  Is c a {2,3} number?\n");
    printf("  c / pi     = %.6f\n", c / M_PI);
    printf("  c * 9      = %.6f\n", c * 9);
    printf("  c * 9 / pi = %.6f\n", c * 9 / M_PI);
    printf("  c * 3      = %.6f\n", c * 3);
    printf("  c * 6      = %.6f\n", c * 6);

    /* What if the correction is alpha/(something structural)? */
    printf("\n  delta_gap = %.6e\n", delta_gap);
    printf("  alpha/pi   = %.6e\n", alpha/M_PI);
    printf("  alpha/3    = %.6e\n", alpha/3);
    printf("  alpha/6    = %.6e\n", alpha/6);
    printf("  alpha/(3pi) = %.6e\n", alpha/(3*M_PI));
    printf("  alpha²     = %.6e\n", alpha*alpha);
    printf("  2*alpha/9  = %.6e\n", 2*alpha/9);

    double ratio_gap_to_a_over_3pi = delta_gap / (alpha/(3*M_PI));
    printf("\n  delta_gap / (alpha/(3pi)) = %.6f\n", ratio_gap_to_a_over_3pi);

    /* Try: delta = 2/9 × (1 + f(alpha)) */
    printf("\nMULTIPLICATIVE CORRECTION: delta = (2/9)(1 + epsilon)\n\n");
    double eps = delta_exact / delta_approx - 1.0;
    printf("  epsilon = %.8f\n", eps);
    printf("  epsilon / alpha = %.6f\n", eps / alpha);
    printf("  epsilon / (alpha/pi) = %.6f\n", eps / (alpha/M_PI));
    printf("  epsilon / (alpha²) = %.6f\n", eps / (alpha*alpha));
    printf("  epsilon * 137 = %.6f\n", eps / alpha);

    /* What if epsilon = alpha/pi? (Schwinger-like correction) */
    double delta_schwinger = delta_approx * (1.0 + alpha/M_PI);
    printf("\n  delta_Schwinger = (2/9)(1 + alpha/pi) = %.10f\n", delta_schwinger);
    printf("  delta_exact     = %.10f\n", delta_exact);
    printf("  New gap: %.6e (%.4f%%)\n",
           fabs(delta_schwinger - delta_exact),
           fabs(delta_schwinger/delta_exact - 1)*100);

    /* Does Schwinger correction fix the masses? */
    printf("\nMASSES WITH delta_Schwinger:\n\n");
    double M_pred = sqrt(mp / 3.0);
    double d = delta_schwinger;
    double mt_p = pow(M_pred*(1+A*cos(d)), 2);
    double me_p = pow(M_pred*(1+A*cos(d+2*M_PI/3)), 2);
    double mmu_p = pow(M_pred*(1+A*cos(d+4*M_PI/3)), 2);

    printf("  m_tau: %.4f MeV (meas: %.4f, %.4f%%)\n",
           mt_p, mtau, fabs(mt_p-mtau)/mtau*100);
    printf("  m_mu:  %.5f MeV (meas: %.5f, %.4f%%)\n",
           mmu_p, mmu, fabs(mmu_p-mmu)/mmu*100);
    printf("  m_e:   %.7f MeV (meas: %.7f, %.4f%%)\n",
           me_p, me, fabs(me_p-me)/me*100);

    /* Try alpha/(2pi) correction (one-loop) */
    printf("\n\ndelta = (2/9)(1 + alpha/(2pi)):\n");
    double d2 = delta_approx * (1.0 + alpha/(2*M_PI));
    mt_p = pow(M_pred*(1+A*cos(d2)), 2);
    me_p = pow(M_pred*(1+A*cos(d2+2*M_PI/3)), 2);
    mmu_p = pow(M_pred*(1+A*cos(d2+4*M_PI/3)), 2);
    printf("  m_tau: %.4f MeV (%.4f%%)\n", mt_p, fabs(mt_p-mtau)/mtau*100);
    printf("  m_mu:  %.5f MeV (%.4f%%)\n", mmu_p, fabs(mmu_p-mmu)/mmu*100);
    printf("  m_e:   %.7f MeV (%.4f%%)\n", me_p, fabs(me_p-me)/me*100);

    /* Try delta = 2/9 + alpha²*k for some k */
    printf("\n\nSWEEP: delta = 2/9 + alpha^n * k\n\n");
    /* We need delta_gap = k * alpha^n */
    for (int n = 1; n <= 3; n++) {
        double an = pow(alpha, n);
        double k = delta_gap / an;
        printf("  n=%d: k = delta_gap/alpha^%d = %.6f", n, n, k);
        if (n == 1) {
            printf("  (k/pi=%.4f, k*3=%.4f, k*9=%.4f)", k/M_PI, k*3, k*9);
        }
        printf("\n");
    }

    /* The real question: does the gap close if we use the
     * RUNNING alpha at the proton mass scale instead of alpha(0)? */
    printf("\n\nRUNNING ALPHA:\n\n");
    /* alpha at m_p ≈ 1 GeV:
     * Only e,mu,u,d,s contribute below 1 GeV
     * Σ Q² = 1 + 1 + 3*(4/9) + 3*(1/9) + 3*(1/9) = 2 + 4/3 + 1/3 + 1/3 = 4
     * b0_QED = 4/3 * 4 = 16/3? No...
     * Actually at 1 GeV: e, mu (not tau), u, d, s (not c,b,t)
     * Σ(Nc*Qf²) = 1(1) + 1(1) + 3(4/9) + 3(1/9) + 3(1/9)
     *           = 1 + 1 + 4/3 + 1/3 + 1/3 = 2 + 2 = 4
     * Hmm let me be more careful:
     * e: Nc=1, Q=1, NcQ²=1
     * mu: Nc=1, Q=1, NcQ²=1
     * u: Nc=3, Q=2/3, NcQ²=4/3
     * d: Nc=3, Q=1/3, NcQ²=1/3
     * s: Nc=3, Q=1/3, NcQ²=1/3
     * Total = 1+1+4/3+1/3+1/3 = 2+2 = 4
     * Running: 1/alpha(mu) = 1/alpha(0) - (4/3)*4/(2pi) * ln(mu²/me²)
     *   Hmm, the sign depends on convention. QED makes alpha LARGER at higher Q.
     *   1/alpha(Q) = 1/alpha(0) - (2/(3pi)) * Σ(NcQf²) * ln(Q/me)
     */
    double sum_NcQ2 = 4.0; /* at 1 GeV: e,mu,u,d,s */
    double ln_ratio = log(mp / me); /* ln(m_p/m_e) ≈ ln(1836) */
    double inv_alpha_mp = 1.0/alpha - (2.0/(3.0*M_PI)) * sum_NcQ2 * ln_ratio;
    double alpha_mp = 1.0 / inv_alpha_mp;
    printf("  alpha(0)   = 1/%.4f\n", 1/alpha);
    printf("  alpha(m_p) = 1/%.4f\n", inv_alpha_mp);
    printf("  ln(m_p/m_e) = %.4f\n", ln_ratio);

    /* Use alpha(m_p) in the correction */
    double delta_corrected = delta_approx * (1.0 + alpha_mp/M_PI);
    printf("\n  delta = (2/9)(1 + alpha(m_p)/pi) = %.10f\n", delta_corrected);
    printf("  delta_exact = %.10f\n", delta_exact);
    printf("  Gap: %.6e (%.4f%%)\n",
           fabs(delta_corrected - delta_exact),
           fabs(delta_corrected/delta_exact - 1)*100);

    /* What about the SIMPLEST possible correction?
     * What if it's not alpha-dependent at all?
     * What if delta = 2/9 is just the leading term of a {2,3} series? */
    printf("\n\n{2,3} SERIES FOR DELTA:\n\n");

    /* delta = 2/9 + 2/9^2 + ... = geometric series? */
    double delta_geo = (2.0/9.0) / (1.0 - 1.0/9.0); /* = 2/9 * 9/8 = 2/8 = 1/4 */
    printf("  2/9 + 2/81 + 2/729 + ... = (2/9)/(1-1/9) = 1/4 = %.10f\n", delta_geo);
    printf("  delta_exact = %.10f  (%.4f%%)\n",
           delta_exact, fabs(delta_geo/delta_exact - 1)*100);

    /* 2/9 + 2/81 (two terms) */
    double delta_2terms = 2.0/9.0 + 2.0/81.0;
    printf("  2/9 + 2/81 = %.10f\n", delta_2terms);
    printf("  delta_exact = %.10f  (%.4f%%)\n",
           delta_exact, fabs(delta_2terms/delta_exact - 1)*100);

    /* 2/9 + 2/(9*81) */
    double delta_mixed = 2.0/9.0 + 2.0/(9.0*81.0);
    printf("  2/9 + 2/729 = %.10f\n", delta_mixed);
    printf("  delta_exact = %.10f  (%.4f%%)\n",
           delta_exact, fabs(delta_mixed/delta_exact - 1)*100);

    /* What if delta = 2·sum(1/3^(2k+1), k=0,1,2,...)?
     * = 2(1/3 + 1/27 + 1/243 + ...) = 2*(1/3)/(1-1/9) = 2/3 * 9/8 = 3/4
     * Too big. */

    /* Try: delta = (2/9)(1 + 2/9 + (2/9)^2 + ...) = (2/9)/(1-2/9) = 2/7 */
    double delta_27 = 2.0/7.0;
    printf("  2/7 = %.10f  (%.4f%%)\n",
           delta_27, fabs(delta_27/delta_exact - 1)*100);

    /* What about delta = arctan(2/9)? Or arcsin(2/9)? */
    printf("\n  arctan(2/9) = %.10f  (%.4f%%)\n",
           atan(2.0/9.0), fabs(atan(2.0/9.0)/delta_exact - 1)*100);
    printf("  arcsin(2/9) = %.10f  (%.4f%%)\n",
           asin(2.0/9.0), fabs(asin(2.0/9.0)/delta_exact - 1)*100);

    /* OK let me just find what rational a/b (small b) is closest */
    printf("\nBEST RATIONAL APPROXIMATIONS (b ≤ 100):\n\n");
    double best_err = 1e10;
    int best_a = 0, best_b = 1;
    for (int b = 1; b <= 100; b++) {
        int a = (int)(delta_exact * b + 0.5);
        if (a > 0) {
            double err = fabs((double)a/b - delta_exact);
            if (err < best_err) {
                best_err = err;
                best_a = a;
                best_b = b;
                printf("  %d/%d = %.10f (err: %.2e, %.4f%%)\n",
                       a, b, (double)a/b, err, err/delta_exact*100);
            }
        }
    }

    /* Now check: does the best rational have {2,3} structure? */
    printf("\nBest: %d/%d\n", best_a, best_b);

    /* Final: what's the electron mass for each candidate delta? */
    printf("\n\nELECTRON MASS SENSITIVITY:\n\n");
    printf("  %-20s  %-12s  %-12s  %-8s\n",
           "delta", "value", "m_e (MeV)", "error");
    printf("  %-20s  %-12s  %-12s  %-8s\n",
           "────────────────────", "────────────", "────────────", "────────");

    double deltas[][2] = {
        {2.0/9.0, 0},
        {2.0/9.0 * (1+alpha/M_PI), 0},
        {2.0/9.0 * (1+alpha/(2*M_PI)), 0},
        {2.0/9.0 + 2.0/81.0, 0},
        {2.0/7.0, 0},
        {atan(2.0/9.0), 0},
        {asin(2.0/9.0), 0},
        {delta_exact, 0},
    };
    const char *names[] = {
        "2/9",
        "(2/9)(1+a/pi)",
        "(2/9)(1+a/(2pi))",
        "2/9 + 2/81",
        "2/7",
        "arctan(2/9)",
        "arcsin(2/9)",
        "exact",
    };
    int nd = 8;

    for (int i = 0; i < nd; i++) {
        double dd = deltas[i][0];
        double me_test = pow(M_pred*(1+A*cos(dd+2*M_PI/3)), 2);
        printf("  %-20s  %.10f  %.7f  %.2f%%\n",
               names[i], dd, me_test, fabs(me_test-me)/me*100);
    }

    /* The key question: what physical process shifts delta from 2/9? */
    printf("\n\nPHYSICAL INTERPRETATION:\n\n");
    printf("  delta = 2/9 is the BARE phase offset.\n");
    printf("  The measured delta includes radiative corrections.\n\n");
    printf("  In QED, the anomalous magnetic moment:\n");
    printf("    g = 2(1 + alpha/(2pi) + ...)\n");
    printf("    The first correction is alpha/(2pi) = Schwinger term.\n\n");
    printf("  If delta gets the same correction:\n");
    printf("    delta = (2/9)(1 + alpha/(2pi))\n");
    printf("    = bare × (1 + one-loop radiative correction)\n\n");

    /* How well does each correct b₀ too? */
    printf("\nDOES FIXING DELTA FIX b0?\n\n");
    /* If delta is corrected, M changes, which changes m_p prediction,
     * which changes the hierarchy ratio, which changes b₀ needed.
     *
     * Actually no — b₀ comes from the Planck-to-proton hierarchy
     * which is independent of the lepton sector. The delta correction
     * and the b₀ correction are DIFFERENT problems.
     *
     * Unless: the b₀ correction IS the same radiative correction.
     * b₀(bare) = 6π, b₀(dressed) = 6π(1 + alpha/(2pi))
     */
    double b0_dressed = 6*M_PI * (1 + alpha/(2*M_PI));
    printf("  b0(bare)    = 6pi = %.8f\n", 6*M_PI);
    printf("  b0(dressed) = 6pi(1+a/(2pi)) = %.8f\n", b0_dressed);
    printf("  b0(exact)   = %.8f\n", b0_exact);
    printf("  Dressed gap: %.4f%%\n", (b0_dressed/b0_exact - 1)*100);

    /* Try: b₀ = 6π(1 + alpha/pi) */
    double b0_schwinger = 6*M_PI * (1 + alpha/M_PI);
    printf("\n  b0 = 6pi(1+a/pi) = %.8f\n", b0_schwinger);
    printf("  b0(exact)         = %.8f\n", b0_exact);
    printf("  Gap: %.4f%%\n", (b0_schwinger/b0_exact - 1)*100);

    /* Try: b₀ = 6π + 2/9 */
    double b0_plus_delta = 6*M_PI + 2.0/9.0;
    printf("\n  b0 = 6pi + 2/9 = %.8f\n", b0_plus_delta);
    printf("  b0(exact)       = %.8f\n", b0_exact);
    printf("  Gap: %.4f%%\n", (b0_plus_delta/b0_exact - 1)*100);

    /* b₀ = 6π + delta_exact? */
    double b0_plus_dex = 6*M_PI + delta_exact;
    printf("\n  b0 = 6pi + delta = %.8f\n", b0_plus_dex);
    printf("  Gap: %.4f%%\n", (b0_plus_dex/b0_exact - 1)*100);

    /* b₀ = 2π(3 + delta)? */
    double b0_2pi3d = 2*M_PI*(3 + delta_approx);
    printf("\n  b0 = 2pi(3+2/9) = 2pi*29/9 = %.8f\n", b0_2pi3d);
    printf("  Gap: %.4f%%\n", (b0_2pi3d/b0_exact - 1)*100);

    /* b₀ = 2π × 29/9 = 58π/9 */
    printf("  = 58*pi/9 = %.8f\n", 58*M_PI/9);
    printf("  58 = 2 * 29. 29 is prime.\n");
    printf("  29 = 3^3 + 2 = 27 + 2. Hmm.\n");
    printf("  Or: 29/9 = 3 + 2/9 = N_c + delta. YES.\n\n");

    printf("  b0 = 2*pi*(N_c + delta)\n");
    printf("     = 2*pi*(3 + 2/9)\n");
    printf("     = 2*pi*29/9\n");
    printf("     = %.8f\n\n", 2*M_PI*(3.0 + 2.0/9.0));

    /* Check how good this is */
    double b0_final = 2*M_PI*(3.0 + 2.0/9.0);
    double ratio_mp = exp(-2*M_PI/(b0_final * alpha));
    double ratio_actual = mp_kg / mpl;
    printf("  With b0 = 2pi(3+2/9):\n");
    printf("    m_p/m_Pl(pred)   = %.6e\n", ratio_mp);
    printf("    m_p/m_Pl(actual) = %.6e\n", ratio_actual);
    printf("    ratio: %.6f\n", ratio_mp/ratio_actual);
    printf("    Previous (6pi): %.6f\n",
           exp(-2*M_PI/(6*M_PI*alpha)) / ratio_actual);

    return 0;
}
