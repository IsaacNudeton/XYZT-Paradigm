#include <stdio.h>
#include <math.h>
int main(void) {
    /* Neutrinos: d=1 waveguide (only weak boundary) */
    /* Q = (2+1)/6 = 1/2  →  A² = 1  →  A = 1 */
    /* Same Z₃ rotation: 3 generations at 120° */
    /* Same δ = 2/9 (universal, from N_c²) */
    
    double d = 2.0/9.0;
    double A = 1.0;  /* d=1 waveguide */
    
    /* Mode factors: r_i = 1 + A·cos(δ + 2πi/3) */
    double r0 = 1 + A*cos(d);              /* heaviest */
    double r1 = 1 + A*cos(d + 2*M_PI/3);   /* lightest */
    double r2 = 1 + A*cos(d + 4*M_PI/3);   /* middle */
    
    printf("NEUTRINO MASSES FROM {2,3} WAVEGUIDE\n\n");
    printf("  d=1 (weak-only boundary), A=1, delta=2/9\n\n");
    printf("  r0 = %.6f (heaviest)\n", r0);
    printf("  r1 = %.6f (lightest)\n", r1);
    printf("  r2 = %.6f (middle)\n\n", r2);
    
    /* Mass RATIOS (don't need M_ν for these) */
    double m_ratio_21 = (r1*r1)/(r0*r0);  /* m_lightest/m_heaviest */
    double m_ratio_20 = (r2*r2)/(r0*r0);  /* m_middle/m_heaviest */
    
    printf("  Mass ratios (to heaviest):\n");
    printf("    m_light/m_heavy = %.6f\n", m_ratio_21);
    printf("    m_mid/m_heavy   = %.6f\n\n", m_ratio_20);
    
    /* Oscillation data constraints */
    double dm2_21 = 7.42e-5;  /* eV², solar */
    double dm2_31 = 2.515e-3; /* eV², atmospheric, normal ordering */
    
    printf("  Oscillation data (PDG):\n");
    printf("    dm²_21 = %.2e eV²\n", dm2_21);
    printf("    dm²_31 = %.3e eV²\n\n", dm2_31);
    
    /* If m1 ≈ 0 (hierarchical): m3 ≈ √dm²_31, m2 ≈ √dm²_21 */
    double m3_min = sqrt(dm2_31);
    double m2_min = sqrt(dm2_21);
    printf("  Minimum masses (m1≈0, normal ordering):\n");
    printf("    m3 ≈ √dm²_31 = %.4f eV\n", m3_min);
    printf("    m2 ≈ √dm²_21 = %.5f eV\n", m2_min);
    printf("    Sum_min ≈ %.4f eV = %.2f meV\n\n", m3_min+m2_min, (m3_min+m2_min)*1000);
    
    /* What does our framework predict for m2/m3? */
    /* Assign: r0 → ν3 (heaviest), r2 → ν2 (middle), r1 → ν1 (lightest) */
    double pred_m2_over_m3 = (r2*r2)/(r0*r0);
    double obs_m2_over_m3 = m2_min/m3_min;
    
    printf("  Framework prediction: m2/m3 = (r2/r0)² = %.5f\n", pred_m2_over_m3);
    printf("  Oscillation minimum: m2/m3 = %.5f\n", obs_m2_over_m3);
    printf("  Ratio: %.3f\n\n", pred_m2_over_m3/obs_m2_over_m3);
    
    /* Use dm²_31 to fix M_ν, then predict everything */
    /* m3 = (r0 × M_ν)², m2 = (r2 × M_ν)², m1 = (r1 × M_ν)² */
    /* dm²_31 = m3² - m1² ≈ m3² (if m1 ≈ 0) */
    /* Wait: dm² = m3² - m1², but m = (r×M)², so m² = (r×M)⁴ */
    /* No: dm² is the difference of mass-squared, and m itself = (r×M_ν)² */
    /* So dm²_31 = m3 - m1 when both are small? No. */
    /* dm²_31 = m3² - m1² = (r0·M_ν)⁴ - (r1·M_ν)⁴ */
    /* With √m_i = r_i·M_ν (Koide param), m_i = r_i²·M_ν² */
    /* dm²_31 = r0⁴·M_ν⁴ - r1⁴·M_ν⁴ = M_ν⁴·(r0⁴ - r1⁴) */
    
    double dr4 = pow(r0,4) - pow(r1,4);
    double Mnu4 = dm2_31 / dr4;  /* M_ν⁴ in eV² */
    double Mnu = pow(Mnu4, 0.25); /* M_ν in √eV */
    
    printf("  Fixing M_ν from dm²_31:\n");
    printf("    M_ν⁴·(r0⁴-r1⁴) = dm²_31\n");
    printf("    r0⁴ - r1⁴ = %.6f\n", dr4);
    printf("    M_ν = %.6f √eV\n\n", Mnu);
    
    /* Predicted masses */
    double m1 = r1*r1*Mnu*Mnu;
    double m2 = r2*r2*Mnu*Mnu;
    double m3 = r0*r0*Mnu*Mnu;
    double sum_mnu = m1 + m2 + m3;
    
    printf("  PREDICTED NEUTRINO MASSES:\n\n");
    printf("    m1 = %.5f eV = %.3f meV\n", m1, m1*1000);
    printf("    m2 = %.5f eV = %.3f meV\n", m2, m2*1000);
    printf("    m3 = %.5f eV = %.3f meV\n", m3, m3*1000);
    printf("    Sum = %.5f eV = %.2f meV\n\n", sum_mnu, sum_mnu*1000);
    
    /* Check dm² consistency */
    double dm2_21_pred = m2*m2 - m1*m1;
    double dm2_31_pred = m3*m3 - m1*m1;
    printf("  CONSISTENCY CHECK:\n");
    printf("    dm²_21(pred) = %.3e eV² (obs: %.2e)\n", dm2_21_pred, dm2_21);
    printf("    dm²_31(pred) = %.3e eV² (obs: %.3e) [input]\n", dm2_31_pred, dm2_31);
    printf("    dm²_21 ratio: %.3f\n\n", dm2_21_pred/dm2_21);
    
    /* Cosmological bound */
    printf("  TESTABLE:\n");
    printf("    Planck 2018 bound: Sum < 120 meV\n");
    printf("    DESI 2024+:       Sum < ~70 meV  \n");
    printf("    Our prediction:   Sum = %.1f meV\n", sum_mnu*1000);
    printf("    Euclid target:    sensitivity ~20-50 meV\n\n");
    
    /* Compare to oscillation-only minimum */
    printf("  Minimum possible (m1=0): %.1f meV\n", (m2_min+m3_min)*1000);
    printf("  Our prediction:          %.1f meV\n", sum_mnu*1000);
    printf("  Excess over minimum:     %.1f meV (from m1 > 0)\n\n",
           (sum_mnu - m2_min - m3_min)*1000);
    
    /* m_d/m_u from the framework */
    printf("═══════════════════════════════════════════════\n");
    printf("  BONUS: m_d/m_u FROM CHARGE RATIO\n");
    printf("═══════════════════════════════════════════════\n\n");
    printf("  cos²θ = 1/3 (down charge) = trapped fraction\n");
    printf("  sin²θ = 2/3 (up charge) = free fraction\n");
    printf("  MORE trapped → MORE mass (confinement = mass)\n\n");
    printf("  Base ratio: m_d/m_u = sin²/cos² = 2\n");
    printf("  QCD correction: × (1 + α_s(2GeV)/π)\n");
    printf("    α_s(2GeV) ≈ 0.30\n");
    printf("    m_d/m_u = 2 × (1 + 0.30/π) = 2 × 1.0955 = %.3f\n", 2*(1+0.30/M_PI));
    printf("  Measured: m_d/m_u = 2.16 (+0.49 -0.26)\n");
    printf("  Match: %.1f%% (well within error bars)\n\n",
           fabs(2*(1+0.30/M_PI)-2.16)/2.16*100);
    
    printf("  Wait — sin²/cos² = (N_c-1)/1 = 2.\n");
    printf("  MORE reflection = MORE trapped energy = MORE mass.\n");
    printf("  The heavier quark is the MORE reflected one.\n");
    printf("  Charges aren't just labels. They're confinement fractions.\n");
    printf("  Mass comes from bouncing. More bouncing = more mass.\n");
    
    return 0;
}
