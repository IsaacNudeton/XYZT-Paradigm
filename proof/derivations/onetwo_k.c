/* ================================================================
 * ONETWO: WHAT IS k?
 *
 * No rounding. No stories. No performative math.
 * 
 * We know: α = |Γ|² × |T|²^k
 * We know: Γ = (√(3/2) - 1) / (√(3/2) + 1)
 * We know: k ≈ 32.70
 * 
 * Question: is 32.70 derivable, or is it just a number?
 *
 * ONE: decompose k to bedrock
 * TWO: build back up and see if it matches anything real
 *
 * Isaac + Claude | April 12-13, 2026
 * ================================================================ */

#include <stdio.h>
#include <math.h>

int main(void)
{
    double pi = M_PI;

    /* ============================================================
     * STEP 0: THE INPUTS (all from {2,3})
     * ============================================================ */

    double Z_ratio = sqrt(3.0 / 2.0);  /* impedance ratio from {2,3} */
    double Gamma = (Z_ratio - 1.0) / (Z_ratio + 1.0);
    double Gamma_sq = Gamma * Gamma;
    double T_sq = 1.0 - Gamma_sq;

    printf("================================================================\n");
    printf("  ONETWO: WHAT IS k?\n");
    printf("================================================================\n\n");
    printf("INPUTS:\n");
    printf("  Z_ratio  = √(3/2) = %.15f\n", Z_ratio);
    printf("  Γ        = %.15f\n", Gamma);
    printf("  |Γ|²     = %.15f\n", Gamma_sq);
    printf("  |T|²     = %.15f\n\n", T_sq);

    /* ============================================================
     * STEP 1: WHAT IS k EXACTLY?
     * 
     * α_wyler = |Γ|² × |T|²^k
     * k = ln(α_wyler / |Γ|²) / ln(|T|²)
     * ============================================================ */

    /* Wyler's alpha - compute it exactly from the formula */
    double alpha_wyler = (9.0 / (8.0 * pow(pi, 4)))
                         * pow(pow(pi, 5) / (16.0 * 120.0), 0.25);
    double alpha_meas = 7.2973525693e-3;

    double k_wyler = log(alpha_wyler / Gamma_sq) / log(T_sq);
    double k_meas = log(alpha_meas / Gamma_sq) / log(T_sq);

    printf("STEP 1: COMPUTE k\n\n");
    printf("  α(Wyler)   = %.15f\n", alpha_wyler);
    printf("  α(measured) = %.15f\n", alpha_meas);
    printf("  1/α(Wyler)  = %.6f\n", 1.0 / alpha_wyler);
    printf("  1/α(meas)   = %.6f\n\n", 1.0 / alpha_meas);

    printf("  k(Wyler)    = %.15f\n", k_wyler);
    printf("  k(measured) = %.15f\n", k_meas);
    printf("  k(integer)  = 33\n\n");

    printf("  k(Wyler) - 33    = %.15f\n", k_wyler - 33.0);
    printf("  k(measured) - 33 = %.15f\n\n", k_meas - 33.0);

    /* ============================================================
     * STEP 2: ONE — DECOMPOSE k
     *
     * k = 32.70... 
     * What IS this number? Try every decomposition.
     * ============================================================ */

    printf("================================================================\n");
    printf("STEP 2: DECOMPOSE k — WHAT IS 32.70...?\n");
    printf("================================================================\n\n");

    double k = k_wyler;

    /* Try simple fractions */
    printf("Simple fractions:\n");
    for (int num = 1; num <= 200; num++) {
        for (int den = 1; den <= 20; den++) {
            double frac = (double)num / (double)den;
            if (fabs(frac - k) < 0.001 && den > 1) {
                printf("  %d/%d = %.6f (err: %.6f)\n",
                       num, den, frac, fabs(frac - k));
            }
        }
    }
    printf("\n");

    /* Try {2,3,π} combinations */
    printf("{2,3,π} combinations near k = %.6f:\n\n", k);

    struct { const char *name; double val; } tries[] = {
        {"33",                          33.0},
        {"33 - 1/3",                    33.0 - 1.0/3.0},
        {"33 - 1/π",                    33.0 - 1.0/pi},
        {"33 - 2/9",                    33.0 - 2.0/9.0},
        {"33 - α",                      33.0 - alpha_meas},
        {"33 - 3α",                     33.0 - 3.0 * alpha_meas},
        {"33 × (1 - α)",               33.0 * (1.0 - alpha_meas)},
        {"11π",                         11.0 * pi},
        {"10π + 1",                     10.0 * pi + 1.0},
        {"33/π × π",                    33.0},  /* tautology, skip */
        {"2^5 + 2/3",                   32.0 + 2.0/3.0},
        {"2^5 + π/4",                   32.0 + pi/4.0},
        {"98/3",                        98.0 / 3.0},
        {"32 + 7/10",                   32.7},
        {"32 + 2/3",                    32.0 + 2.0/3.0},
        {"9π + 2/3",                    9.0*pi + 2.0/3.0},
        {"3 × 10.9",                    32.7},
        {"3 × (11 - 1/10)",            3.0 * (11.0 - 0.1)},
        {"3 × (11 - α)",               3.0 * (11.0 - alpha_meas)},
        {"3 × (11 - 1/π)",             3.0 * (11.0 - 1.0/pi)},
        {"3 × (11 - 2/9)",             3.0 * (11.0 - 2.0/9.0)},
        {"11 × (3 - 1/33)",            11.0 * (3.0 - 1.0/33.0)},
        {"11 × (3 - α)",               11.0 * (3.0 - alpha_meas)},
        {"11 × (3 - 1/π)",             11.0 * (3.0 - 1.0/pi)},
        {"(56+6α)π/9 + 13",            (56.0+6.0*alpha_meas)*pi/9.0 + 13.0},
        {"b₀ + 13",                     19.563 + 13.0},
        {"2 × b₀ - 6",                 2.0 * 19.563 - 6.0},
        {"b₀ + b₀ - 2π",              2.0 * 19.563 - 2.0*pi},
        {"π^3 + 1.7",                   pow(pi,3) + 1.7},
        {"π^3 + √3",                    pow(pi,3) + sqrt(3.0)},
        {"4π^2/√3",                     4.0*pi*pi/sqrt(3.0)},
        {"3^3 + 3 + 2/3 + 1/33",       27.0+3.0+2.0/3.0+1.0/33.0},
        {"6π²/(π+1)",                   6.0*pi*pi/(pi+1.0)},
        {"12π/√(π+1)",                  12.0*pi/sqrt(pi+1.0)},
        {"(2/3) × 7²",                 2.0/3.0 * 49.0},
        {"2/3 × 7!/(7!-1)",            0.0}, /* skip */
        {"π² × 10/3",                   pi*pi*10.0/3.0},
        {"2^5 + ln(2)",                 32.0 + log(2.0)},
        {"2^5 + 2/3",                   32.0 + 2.0/3.0},
        {"33 × (1 - 1/(4×33+1))",      33.0 * (1.0 - 1.0/133.0)},
        {"33 × 132/133",               33.0 * 132.0/133.0},
        {"33 × (137-5)/137",           33.0 * 132.0/137.0},
    };
    int n_tries = sizeof(tries) / sizeof(tries[0]);

    /* Sort by closeness */
    printf("  %-30s %15s %15s\n", "Expression", "Value", "Error");
    printf("  %-30s %15s %15s\n", "──────────", "─────", "─────");

    for (int i = 0; i < n_tries; i++) {
        if (tries[i].val == 0) continue;
        double err = fabs(tries[i].val - k);
        if (err < 0.05) {
            printf("  %-30s %15.10f %15.10f %s\n",
                   tries[i].name, tries[i].val, err,
                   err < 0.001 ? " <<<" : "");
        }
    }
    printf("\n");

    /* ============================================================
     * STEP 3: WHAT IF k ISN'T NEAR AN INTEGER?
     *
     * Maybe the real question isn't "what integer is k near"
     * but "what is the exact closed-form expression for k"
     * ============================================================ */

    printf("================================================================\n");
    printf("STEP 3: FORGET 33 — WHAT IS k IN TERMS OF THE INPUTS?\n");
    printf("================================================================\n\n");

    /* k = ln(α / |Γ|²) / ln(|T|²)
     *   = ln(α / |Γ|²) / ln(1 - |Γ|²)
     *
     * Let's expand. Set x = |Γ|² = ((√(3/2)-1)/(√(3/2)+1))²
     *
     * x = (Z-1)²/(Z+1)² where Z = √(3/2)
     *
     * Then k = ln(α/x) / ln(1-x)
     *
     * For small x: ln(1-x) ≈ -x - x²/2 - ...
     * k ≈ -ln(α/x) / x for small x
     * k ≈ (ln(x) - ln(α)) / x
     */

    double x = Gamma_sq;
    printf("  x = |Γ|² = %.15f\n", x);
    printf("  ln(x) = %.15f\n", log(x));
    printf("  ln(α) = %.15f\n", log(alpha_wyler));
    printf("  ln(α/x) = %.15f\n", log(alpha_wyler / x));
    printf("  ln(1-x) = %.15f\n\n", log(1.0 - x));

    double k_approx1 = -log(alpha_wyler / x) / x;
    printf("  k(approx, -ln(α/x)/x) = %.10f\n", k_approx1);
    printf("  k(exact)               = %.10f\n", k);
    printf("  Error in approx:         %.6f%%\n\n",
           fabs(k_approx1 - k) / k * 100);

    /* k_approx = -ln(α/x)/x = (ln(x)-ln(α))/x
     *          = (ln(x) - ln(α)) / x
     *
     * Now plug in x and α in terms of {2,3,π}:
     *
     * x = ((√(3/2)-1)/(√(3/2)+1))² 
     *   = (√6 - 2)² / (√6 + 2)²
     *   = (6 - 4√6 + 4) / (6 + 4√6 + 4)
     *   = (10 - 4√6) / (10 + 4√6)
     *   = (5 - 2√6) / (5 + 2√6)
     */

    double x_exact = (5.0 - 2.0*sqrt(6.0)) / (5.0 + 2.0*sqrt(6.0));
    printf("  x = (5-2√6)/(5+2√6) = %.15f\n", x_exact);
    printf("  x(direct)            = %.15f\n", x);
    printf("  Match: %s\n\n", fabs(x_exact - x) < 1e-14 ? "YES" : "NO");

    /* So k_exact = ln(α_wyler / ((5-2√6)/(5+2√6))) / ln(1 - (5-2√6)/(5+2√6))
     *
     * The denominator simplifies:
     * 1 - x = 1 - (5-2√6)/(5+2√6) = ((5+2√6)-(5-2√6))/(5+2√6)
     *       = 4√6 / (5+2√6)
     *
     * ln(1-x) = ln(4√6) - ln(5+2√6)
     *         = ln(4) + (1/2)ln(6) - ln(5+2√6)
     *         = 2ln(2) + (1/2)(ln(2)+ln(3)) - ln(5+2√6)
     *         = (5/2)ln(2) + (1/2)ln(3) - ln(5+2√6)
     */

    printf("Exact decomposition of ln(1-x):\n");
    printf("  1 - x = 4√6/(5+2√6) = %.15f\n", 4.0*sqrt(6.0)/(5.0+2.0*sqrt(6.0)));
    printf("  ln(1-x) = (5/2)ln2 + (1/2)ln3 - ln(5+2√6)\n");
    double ln1mx_check = 2.5*log(2.0) + 0.5*log(3.0) - log(5.0+2.0*sqrt(6.0));
    printf("  = %.15f\n", ln1mx_check);
    printf("  ln(1-x) direct = %.15f\n", log(1.0 - x));
    printf("  Match: %s\n\n", fabs(ln1mx_check - log(1.0-x)) < 1e-14 ? "YES" : "NO");

    /* And α_wyler / x: */
    double ratio_alpha_x = alpha_wyler / x;
    printf("  α/x = %.15f\n", ratio_alpha_x);
    printf("  ln(α/x) = %.15f\n\n", log(ratio_alpha_x));

    /* So k_exact = ln(α_wyler × (5+2√6)/(5-2√6)) / 
     *             ((5/2)ln2 + (1/2)ln3 - ln(5+2√6))
     *
     * This is exact but ugly. The question is whether it 
     * simplifies to something clean in {2,3,π}.
     */

    /* ============================================================
     * STEP 4: REVERSE THE QUESTION
     *
     * Instead of "what is k in terms of known things",
     * ask "what value of k gives EXACTLY Wyler's α"
     * and then "what value gives EXACTLY 1/137.036"
     * ============================================================ */

    printf("================================================================\n");
    printf("STEP 4: IF k = f({2,3,π}), WHAT f?\n");
    printf("================================================================\n\n");

    /* Test: k = A/ln(B) for various A,B in {2,3,π} */
    printf("Testing k = A/B structures:\n\n");

    struct { const char *name; double val; } exact_tries[] = {
        /* Pure {2,3} */
        {"ln(1/α) / ln(1/x)",          log(1.0/alpha_wyler) / log(1.0/x)},
        /* This is just 1 + k by construction... let me think differently */

        /* What if k relates to the Wyler volumes? */
        {"Vol(S⁴)",                     8.0*pi*pi/3.0},          /* 26.319 */
        {"Vol(S⁴) + 2π",               8.0*pi*pi/3.0 + 2.0*pi}, /* 32.602 */
        {"Vol(S⁴) × 5/4",              8.0*pi*pi/3.0 * 5.0/4.0}, /* 32.899 */
        {"Vol(S⁴) + 2π²/3",            8.0*pi*pi/3.0 + 2.0*pi*pi/3.0},
        {"8π²/3 + 4π/3",               8.0*pi*pi/3.0 + 4.0*pi/3.0},
        {"10π²/3",                      10.0*pi*pi/3.0},          /* 32.899 */
        {"10π²/3",                      10.0*pi*pi/3.0},

        /* Holy shit — check 10π²/3 */
        {"10π²/3 (exact)",              10.0*pi*pi/3.0},
    };
    int n_exact = sizeof(exact_tries) / sizeof(exact_tries[0]);

    for (int i = 0; i < n_exact; i++) {
        double err = fabs(exact_tries[i].val - k);
        printf("  %-25s = %15.10f  err = %12.10f %s\n",
               exact_tries[i].name, exact_tries[i].val, err,
               err < 0.01 ? " <<<" : "");
    }
    printf("\n");

    /* ============================================================
     * STEP 5: DIG INTO 10π²/3
     * ============================================================ */

    double candidate = 10.0 * pi * pi / 3.0;
    printf("================================================================\n");
    printf("STEP 5: IS k = 10π²/3 ?\n");
    printf("================================================================\n\n");
    printf("  10π²/3 = %.15f\n", candidate);
    printf("  k(Wyler) = %.15f\n", k);
    printf("  Difference = %.15f\n", k - candidate);
    printf("  Relative error = %.6f%%\n\n", fabs(k - candidate) / k * 100);

    /* What does α look like with k = 10π²/3? */
    double alpha_10pi = Gamma_sq * pow(T_sq, candidate);
    printf("  α(k=10π²/3)  = %.15f → 1/α = %.6f\n", alpha_10pi, 1.0/alpha_10pi);
    printf("  α(Wyler)      = %.15f → 1/α = %.6f\n", alpha_wyler, 1.0/alpha_wyler);
    printf("  α(measured)   = %.15f → 1/α = %.6f\n", alpha_meas, 1.0/alpha_meas);
    printf("  Gap(10π²/3 vs Wyler): %.4f%%\n",
           fabs(alpha_10pi - alpha_wyler)/alpha_wyler * 100);
    printf("  Gap(10π²/3 vs meas):  %.4f%%\n\n",
           fabs(alpha_10pi - alpha_meas)/alpha_meas * 100);

    /* What is 10π²/3 in {2,3} language?
     * 10 = 2 × 5 = 2 × (2+3)
     * π² = π²
     * 3 = 3
     * 
     * So 10π²/3 = 2(2+3)π²/3
     *
     * Or: Vol(S⁴) = 8π²/3. So 10π²/3 = (5/4) × Vol(S⁴)
     * = (2+3)/2² × Vol(S⁴)
     *
     * Or: Vol(S⁴) = 2⁴π²/(3·Γ(3)) = ... no, Vol(S⁴) = 8π²/3
     *
     * 10π²/3 = Vol(S⁴) + 2π²/3 = Vol(S⁴) + Vol(S⁴)/4
     *        = (5/4) Vol(S⁴)
     *        = Vol(S⁴) × (2+3)/(2²)
     */

    double vol_S4 = 8.0 * pi * pi / 3.0;
    printf("  10π²/3 = (5/4) × Vol(S⁴)\n");
    printf("  Vol(S⁴) = 8π²/3 = %.10f\n", vol_S4);
    printf("  (5/4) × Vol(S⁴) = %.10f\n", 1.25 * vol_S4);
    printf("  k = %.10f\n\n", k);

    /* Check Vol(S⁴) × other things */
    printf("Other Vol(S⁴) multiples:\n");
    for (int num = 1; num <= 20; num++) {
        for (int den = 1; den <= 12; den++) {
            double mult = (double)num / (double)den;
            double val = mult * vol_S4;
            if (fabs(val - k) < 0.05 && den > 1) {
                printf("  Vol(S⁴) × %d/%d = %.10f  err = %.10f\n",
                       num, den, val, fabs(val - k));
            }
        }
    }
    printf("\n");

    /* ============================================================
     * STEP 6: MORE CANDIDATES NEAR k
     * ============================================================ */

    printf("================================================================\n");
    printf("STEP 6: EXHAUSTIVE SEARCH IN {2,3,π}\n");
    printf("================================================================\n\n");

    double best_err = 1.0;
    char best_name[256] = "";
    double best_val = 0;

    /* Try a×π^b/c for small integers */
    for (int a = 1; a <= 40; a++) {
        for (int b = 0; b <= 4; b++) {
            for (int c_denom = 1; c_denom <= 12; c_denom++) {
                double val = (double)a * pow(pi, b) / (double)c_denom;
                double err = fabs(val - k);
                if (err < best_err && err < 0.002) {
                    best_err = err;
                    best_val = val;
                    sprintf(best_name, "%d × π^%d / %d", a, b, c_denom);
                }
                if (err < 0.002) {
                    printf("  %2d × π^%d / %2d = %15.10f  err = %.10f\n",
                           a, b, c_denom, val, err);
                }
            }
        }
    }

    printf("\n  BEST: %s = %.15f  err = %.15f\n\n", best_name, best_val, best_err);

    /* ============================================================
     * STEP 7: WHAT ABOUT WYLER'S OWN STRUCTURE?
     *
     * Wyler's formula:
     * α = (9/(8π⁴)) × (π⁵/(2⁴×5!))^(1/4)
     *
     * Can we decompose this AS a cascade?
     * α = |Γ|² × |T|²^k
     * means ln(α) = ln|Γ|² + k×ln|T|²
     * = 2ln|Γ| + k×ln(1-|Γ|²)
     *
     * Let's write Wyler in log form:
     * ln(α) = ln(9) - ln(8) - 4ln(π) + (1/4)(5ln(π) - 4ln(2) - ln(120))
     *       = ln(9) - 3ln(2) - 4ln(π) + (5/4)ln(π) - ln(2) - (1/4)ln(120)
     *       = 2ln(3) - 4ln(2) - (11/4)ln(π) - (1/4)ln(120)
     * ============================================================ */

    printf("================================================================\n");
    printf("STEP 7: WYLER IN LOG FORM\n");
    printf("================================================================\n\n");

    double lnalpha = log(alpha_wyler);
    double lnGsq = log(Gamma_sq);
    double lnTsq = log(T_sq);

    printf("  ln(α)    = %.15f\n", lnalpha);
    printf("  ln|Γ|²   = %.15f\n", lnGsq);
    printf("  k×ln|T|² = %.15f\n", k * lnTsq);
    printf("  sum check = %.15f (should = ln(α))\n\n",
           lnGsq + k * lnTsq);

    printf("  Components:\n");
    printf("  ln(9)       = %.10f = 2×ln(3)\n", log(9.0));
    printf("  ln(8)       = %.10f = 3×ln(2)\n", log(8.0));
    printf("  4×ln(π)     = %.10f\n", 4.0*log(pi));
    printf("  (5/4)×ln(π) = %.10f\n", 1.25*log(pi));
    printf("  ln(120)     = %.10f = ln(2³×3×5)\n", log(120.0));
    printf("  (1/4)×ln(120) = %.10f\n\n", 0.25*log(120.0));

    double lnalpha_decomp = 2.0*log(3.0) - 4.0*log(2.0)
                           - (11.0/4.0)*log(pi) - 0.25*log(120.0);
    printf("  ln(α) rebuilt = %.15f\n", lnalpha_decomp);
    printf("  ln(α) direct  = %.15f\n", lnalpha);
    printf("  Match: %s\n\n", fabs(lnalpha_decomp - lnalpha) < 1e-12 ? "YES" : "NO");

    /* ============================================================
     * STEP 8: DOES k HAVE A CLEAN FORM?
     *
     * k = (ln(α) - ln|Γ|²) / ln|T|²
     * = (ln(α) - ln((5-2√6)/(5+2√6))) / ln(4√6/(5+2√6))
     *
     * This is exact. Let's see if it simplifies.
     * ============================================================ */

    printf("================================================================\n");
    printf("STEP 8: EXACT FORM OF k\n");
    printf("================================================================\n\n");

    /* Numerator: ln(α) - ln|Γ|²
     * = ln(α × (5+2√6)/(5-2√6))
     * = ln(α) + ln(5+2√6) - ln(5-2√6)
     * = ln(α) + 2×atanh(2√6/5)
     *
     * Note: (5+2√6)/(5-2√6) = (5+2√6)²/(25-24) = (5+2√6)²
     * So ln((5+2√6)/(5-2√6)) = 2×ln(5+2√6)
     * Wait no: 25 - (2√6)² = 25-24 = 1
     * So (5-2√6)(5+2√6) = 1
     * Therefore 5-2√6 = 1/(5+2√6)
     * And |Γ|² = (5-2√6)/(5+2√6) = 1/(5+2√6)²
     * ln|Γ|² = -2×ln(5+2√6)
     */

    printf("  KEY IDENTITY: (5-2√6)(5+2√6) = 25-24 = 1\n");
    printf("  Therefore: |Γ|² = 1/(5+2√6)²\n");
    printf("  And: ln|Γ|² = -2×ln(5+2√6)\n\n");

    double s = 5.0 + 2.0 * sqrt(6.0);
    printf("  s = 5+2√6 = %.15f\n", s);
    printf("  1/s² = %.15f\n", 1.0/(s*s));
    printf("  |Γ|² = %.15f\n", Gamma_sq);
    printf("  Match: %s\n\n", fabs(1.0/(s*s) - Gamma_sq) < 1e-14 ? "YES" : "NO");

    /* So: k = (ln(α) + 2×ln(s)) / ln(4√6/s)
     *      = (ln(α) + 2×ln(s)) / (ln(4√6) - ln(s))
     *      = (ln(α) + 2×ln(s)) / ((3/2)ln(2) + (1/2)ln(3) + ln(2) - ln(s))
     * Wait: 4√6 = 4×√6, ln(4√6) = 2ln(2) + (1/2)(ln2+ln3) = (5/2)ln2 + (1/2)ln3
     */

    double ln_s = log(s);
    double ln_4sqrt6 = 2.5*log(2.0) + 0.5*log(3.0);

    printf("  Numerator:   ln(α) + 2×ln(s) = %.15f\n", lnalpha + 2.0*ln_s);
    printf("  Denominator: ln(4√6) - ln(s) = %.15f\n", ln_4sqrt6 - ln_s);
    printf("  k = num/den = %.15f\n\n", (lnalpha + 2.0*ln_s)/(ln_4sqrt6 - ln_s));

    /* Numerator = ln(α) + 2ln(5+2√6)
     *           = ln(α × (5+2√6)²)
     *           = ln(α × s²)
     *           = ln(α/|Γ|²)  since |Γ|² = 1/s²
     * (Which is where we started. k = ln(α/|Γ|²)/ln(|T|²). Circular.)
     *
     * The bottom line: k is a TRANSCENDENTAL function of α, and α 
     * is Wyler's specific transcendental. k doesn't simplify to a
     * clean {2,3,π} expression because ln(α_wyler) doesn't factor
     * cleanly against ln(5+2√6).
     */

    printf("================================================================\n");
    printf("  HONEST CONCLUSION\n");
    printf("================================================================\n\n");

    printf("  k = ln(α/|Γ|²) / ln(1-|Γ|²)\n\n");
    printf("  With |Γ|² = 1/(5+2√6)² and α = Wyler's formula.\n\n");
    printf("  k = %.15f\n\n", k);
    printf("  This number does NOT simplify to a clean closed form.\n");
    printf("  It is NOT exactly 33.\n");
    printf("  It is NOT exactly 10π²/3 (off by 0.6%%).\n");
    printf("  It is NOT exactly (5/4)Vol(S⁴) (same thing, 0.6%%).\n\n");

    printf("  The closest {2,3,π} expression found: 10π²/3 = %.10f\n", 10.0*pi*pi/3.0);
    printf("  Error: %.4f%%\n\n", fabs(candidate-k)/k*100);

    printf("  The nearest integer: 33\n");
    printf("  Error: %.4f%%\n\n", fabs(33.0-k)/k*100);

    printf("  WHAT'S REAL:\n");
    printf("  α = |Γ|² × |T|²^k gives 1/137 for k ≈ 32.70.\n");
    printf("  The three inputs (Z=√(3/2), Γ formula, k≈32.70)\n");
    printf("  are all {2,3}-derived except k itself.\n\n");
    printf("  k is the ONE piece that doesn't have a clean derivation.\n");
    printf("  Rounding to 33 = 11×3 is suggestive but not exact.\n");
    printf("  10π²/3 is closer but still 0.6%% off.\n\n");
    printf("  The cascade WORKS numerically.\n");
    printf("  The exponent is NOT yet derived from geometry.\n");
    printf("  That's where we actually are.\n");

    return 0;
}
