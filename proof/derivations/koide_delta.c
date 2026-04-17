#include <stdio.h>
#include <math.h>

int main(void)
{
    double me = 0.51099895, mmu = 105.6583755, mtau = 1776.86;
    double sme = sqrt(me), smmu = sqrt(mmu), smtau = sqrt(mtau);
    double M = (sme + smmu + smtau) / 3.0;
    
    /* Extract exact delta from tau (most stable, far from node) */
    double cos_tau = (smtau/M - 1.0)/sqrt(2.0);
    double delta_exact = acos(cos_tau);
    
    printf("EXACT δ = %.10f rad (from measured masses)\n", delta_exact);
    printf("2/9     = %.10f rad\n", 2.0/9.0);
    printf("Gap     = %.2e rad (%.4f%%)\n\n", 
           fabs(delta_exact - 2.0/9.0), fabs(delta_exact-2.0/9.0)/delta_exact*100);
    
    /* With exact delta, identify which i maps to which lepton */
    printf("MODE ORDERING (which i = which lepton):\n");
    for (int i = 0; i < 3; i++) {
        double c = cos(delta_exact + 2*M_PI*i/3.0);
        double r = 1.0 + sqrt(2.0)*c;
        double sm = r * M;
        double m = sm * sm;
        printf("  i=%d: cos=%.6f  r=%.6f  √m=%.4f  m=%.4f MeV", i, c, r, sm, m);
        if (m > 1000) printf(" ← τ\n");
        else if (m > 1) printf("  ← μ\n");
        else printf("  ← e\n");
    }
    
    /* Now predict with delta = 2/9, correctly labeled */
    printf("\n═══════════════════════════════════════════════════\n");
    printf("  PREDICTION: δ = 2/9 = 2/3²\n");
    printf("═══════════════════════════════════════════════════\n\n");
    
    double d = 2.0/9.0;
    double c0 = cos(d);              /* i=0 → τ */
    double c1 = cos(d + 2*M_PI/3);  /* i=1 → e */
    double c2 = cos(d + 4*M_PI/3);  /* i=2 → μ */
    
    double r_tau = 1.0 + sqrt(2.0)*c0;
    double r_e   = 1.0 + sqrt(2.0)*c1;
    double r_mu  = 1.0 + sqrt(2.0)*c2;
    
    double mtau_p = (r_tau*M)*(r_tau*M);
    double me_p   = (r_e*M)*(r_e*M);
    double mmu_p  = (r_mu*M)*(r_mu*M);
    
    printf("           predicted       measured        error\n");
    printf("  m_τ    %10.3f MeV   %10.3f MeV   %.4f%%\n",
           mtau_p, mtau, fabs(mtau_p-mtau)/mtau*100);
    printf("  m_μ    %10.4f MeV   %10.4f MeV   %.4f%%\n",
           mmu_p, mmu, fabs(mmu_p-mmu)/mmu*100);
    printf("  m_e    %10.6f MeV   %10.6f MeV   %.4f%%\n\n",
           me_p, me, fabs(me_p-me)/me*100);
    
    double sum_pred = mtau_p + mmu_p + me_p;
    double sum_obs = mtau + mmu + me;
    printf("  Σm_ℓ   %10.3f MeV   %10.3f MeV   %.4f%%\n\n",
           sum_pred, sum_obs, fabs(sum_pred-sum_obs)/sum_obs*100);
    
    printf("  T_c = Σm_ℓ(pred)/12 = %.3f MeV\n", sum_pred/12);
    printf("  T_c = m_p/6         = %.3f MeV\n", 938.272/6.0);
    printf("  T_c observed        = 156.5 ± 1.5 MeV\n\n");
    
    /* The electron mass discrepancy analysis */
    printf("═══════════════════════════════════════════════════\n");
    printf("  ELECTRON: WHY IT'S THE HARDEST\n");
    printf("═══════════════════════════════════════════════════\n\n");
    
    /* Sensitivity: dm/dδ near the node */
    double dcos_ddelta = -sin(d + 2*M_PI/3);
    double dr_ddelta = sqrt(2.0) * dcos_ddelta;
    double dm_ddelta = 2 * r_e * M * M * dr_ddelta;
    printf("  dm_e/dδ = %.4f MeV/rad\n", dm_ddelta);
    printf("  Δδ = %.6f rad → Δm_e = %.6f MeV\n", 
           delta_exact - d, dm_ddelta * (delta_exact - d));
    printf("  Sensitivity amplification: %.0f×\n", 
           fabs((me_p-me)/me) / fabs((d-delta_exact)/delta_exact));
    printf("  (0.02%% angle error → %.1f%% mass error)\n\n",
           fabs(me_p-me)/me*100);
    
    /* What's 2/9 structurally */
    printf("═══════════════════════════════════════════════════\n");
    printf("  STRUCTURE OF δ = 2/9\n");
    printf("═══════════════════════════════════════════════════\n\n");
    printf("  2/9 = 2/3²\n");
    printf("  = {2}/{3²}\n");  
    printf("  = distinction / (substrate²)\n");
    printf("  = (spin states) / (color × generation)\n\n");
    printf("  The phase offset that positions the three masses\n");
    printf("  on the Koide circle is the ratio of the binary\n");
    printf("  element to the square of the ternary element.\n\n");
    printf("  This is {2,3} at its most literal.\n");

    return 0;
}
