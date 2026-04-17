#include <stdio.h>
#include <math.h>
int main(void) {
    double A = sqrt(2.0);
    double delta = 2.0/9.0;
    double Nc = 3;
    double mp = 938.272;  /* MeV */
    double hbar_c = 197.327; /* MeV·fm */
    
    printf("THE BRIDGE: PROTON RADIUS = A^4 / m_p\n\n");
    
    /* Proton charge radius measured */
    double rp_meas = 0.8409; /* fm, muonic hydrogen 2019 */
    
    /* Tonight's derivation: r_p = 4*hbar/(m_p*c) */
    double rp_pred = 4.0 * hbar_c / mp;
    printf("  r_p = 4*hbar_c / m_p = %.4f fm\n", rp_pred);
    printf("  r_p measured = %.4f fm\n", rp_meas);
    printf("  Error: %.2f%%\n\n", fabs(rp_pred-rp_meas)/rp_meas*100);
    
    /* A^4 = (sqrt(2))^4 = 4. So the 4 IS A^4. */
    double A4 = pow(A, 4);
    printf("  A^4 = (sqrt(2))^4 = %.1f = 4. Same 4.\n\n", A4);
    
    /* Wyler connection */
    double wyler_89 = 8.0/9.0;
    double four_delta = 4.0 * delta;
    printf("WYLER CONNECTION:\n");
    printf("  Wyler's 8/9 = %.6f\n", wyler_89);
    printf("  4 * delta   = 4 * 2/9 = %.6f\n", four_delta);
    printf("  Same number: %s\n\n", fabs(wyler_89 - four_delta) < 1e-10 ? "YES" : "no");
    
    printf("  8/9 = A^4 * delta = (sqrt(2))^4 * (2/9) = 4 * 2/9\n");
    printf("  The Wyler denominator factor = Koide amplitude^4 * Koide phase\n\n");
    
    /* r_p in terms of waveguide parameters */
    double M2 = mp / Nc;  /* M^2 = m_p/3 */
    double M = sqrt(M2);
    double rp_wg = A4 * hbar_c / (Nc * M2);
    printf("PROTON RADIUS FROM WAVEGUIDE:\n");
    printf("  r_p = A^4 * hbar_c / (N_c * M^2)\n");
    printf("      = %.1f * %.3f / (%.0f * %.3f)\n", A4, hbar_c, Nc, M2);
    printf("      = %.4f fm\n", rp_wg);
    printf("  Same as 4*hbar_c/m_p: %s\n\n", 
           fabs(rp_wg - rp_pred) < 1e-10 ? "YES (algebraically identical)" : "no");
    
    /* The full chain in one place */
    printf("THE FULL CHAIN — EVERY PARAMETER IS {2,3}:\n\n");
    printf("  alpha^-1 = 137     [2^7 + 3^2, E-series unique]\n");
    printf("  Wyler:   8/9       [2^3/3^2 = A^4 * delta]\n");
    printf("  m_p/m_Pl           [exp(-2pi/(b0*alpha)), b0 from N_c=3]\n");
    printf("  M = sqrt(m_p/3)    [3 = N_c]\n");
    printf("  A = sqrt(2)        [from Q = 2/3]\n");
    printf("  delta = 2/9        [2/3^2]\n");
    printf("  T_c = m_p/6        [2*3 = 6]\n");
    printf("  r_p = 4/m_p        [4 = A^4 = 2^2]\n");
    printf("  eta = 2*aW^5*aEM   [2 = dual impedance]\n\n");
    printf("  Integers used: 2, 3, their powers, and pi.\n");
    printf("  Nothing else.\n");
    
    return 0;
}
