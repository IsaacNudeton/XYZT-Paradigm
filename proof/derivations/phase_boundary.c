#include <stdio.h>
#include <math.h>
int main(void) {
    double me=0.51099895, mmu=105.6583755, mtau=1776.86;
    double mp=938.272088, mn=939.565421;
    double mu=2.16, md=4.67, ms=93.4; /* current quark masses, PDG MS-bar 2GeV */
    double sml = me+mmu+mtau;
    double gap = sml - 2*mp;
    
    printf("THE GAP: Sigma_ml - 2m_p\n\n");
    printf("  Sigma_ml = %.3f MeV\n", sml);
    printf("  2*m_p    = %.3f MeV\n", 2*mp);
    printf("  Gap      = %.3f MeV\n\n", gap);
    
    printf("WHAT IS %.3f MeV?\n\n", gap);
    printf("  m_u          = %.2f MeV  -> 3*m_u = %.2f MeV (diff: %.3f)\n",
           mu, 3*mu, fabs(gap-3*mu));
    printf("  m_d          = %.2f MeV  -> 3*m_d = %.2f MeV (diff: %.3f)\n",
           md, 3*md, fabs(gap-3*md));
    printf("  m_u+m_d      = %.2f MeV                      (diff: %.3f)\n",
           mu+md, fabs(gap-(mu+md)));
    printf("  2m_u+m_d     = %.2f MeV  (proton content)    (diff: %.3f)\n",
           2*mu+md, fabs(gap-(2*mu+md)));
    printf("  m_n-m_p      = %.3f MeV                      (diff: %.3f)\n",
           mn-mp, fabs(gap-(mn-mp)));
    printf("  m_pi0        = 134.977 MeV                    (no)\n");
    printf("  alpha*m_p    = %.3f MeV                      (diff: %.3f)\n",
           mp/137.036, fabs(gap-mp/137.036));
    
    printf("\n  >>> 3*m_u = %.2f MeV vs gap = %.3f MeV <<<\n", 3*mu, gap);
    printf("  >>> Match: %.2f%% <<<\n\n", fabs(gap-3*mu)/gap*100);
    
    printf("CORRECTED FORMULA:\n\n");
    printf("  Sigma_ml = 2*m_p + 3*m_u = 2*m_p + N_c*m_u\n");
    printf("  = %.3f + %.2f = %.3f MeV\n", 2*mp, 3*mu, 2*mp+3*mu);
    printf("  Measured: %.3f MeV\n", sml);
    printf("  Error: %.4f%% (%.3f MeV)\n\n", 
           fabs(sml-(2*mp+3*mu))/sml*100, fabs(sml-(2*mp+3*mu)));
    
    printf("THEREFORE:\n\n");
    printf("  M^2 = Sigma_ml/6 = (2m_p + 3m_u)/6 = m_p/3 + m_u/2\n\n");
    double M_exact = sqrt(mp/3.0 + mu/2.0);
    double M_meas = (sqrt(me)+sqrt(mmu)+sqrt(mtau))/3.0;
    printf("  M = sqrt(m_p/3 + m_u/2) = sqrt(%.3f) = %.5f sqrt(MeV)\n",
           mp/3+mu/2, M_exact);
    printf("  M(measured)              = %.5f sqrt(MeV)\n", M_meas);
    printf("  Error: %.4f%%\n\n", fabs(M_exact-M_meas)/M_meas*100);
    
    /* Now predict masses with this M */
    double d = 2.0/9.0;
    double t=pow(M_exact*(1+sqrt(2)*cos(d)),2);
    double u=pow(M_exact*(1+sqrt(2)*cos(d+4*M_PI/3)),2);
    double e=pow(M_exact*(1+sqrt(2)*cos(d+2*M_PI/3)),2);
    printf("MASSES FROM m_p AND m_u ONLY:\n\n");
    printf("  m_tau = %.4f MeV  (meas: %.4f, %.4f%%)\n",t,mtau,fabs(t-mtau)/mtau*100);
    printf("  m_mu  = %.5f MeV (meas: %.5f, %.4f%%)\n",u,mmu,fabs(u-mmu)/mmu*100);
    printf("  m_e   = %.7f MeV (meas: %.7f, %.4f%%)\n",e,me,fabs(e-me)/me*100);
    printf("  Sum   = %.4f MeV  (meas: %.4f, %.5f%%)\n\n",
           t+u+e, sml, fabs(t+u+e-sml)/sml*100);
    
    printf("Two quark masses in. Three lepton masses out.\n");
    printf("The boundary between confined and free sectors\n");
    printf("depends on both the cage (m_p) and the prisoner (m_u).\n");
    return 0;
}
