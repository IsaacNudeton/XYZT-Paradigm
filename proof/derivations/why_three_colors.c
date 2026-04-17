/* ================================================================
 * WHY 3 COLORS?
 * The bottom of the stack. Deriving N_c = 3 from constraints.
 * ================================================================ */

#include <stdio.h>
#include <math.h>

int main(void)
{
    printf("================================================================\n");
    printf("  WHY 3? DERIVING N_c FROM THREE CONSTRAINTS\n");
    printf("================================================================\n\n");
    
    /* ================================================================
     * THREE CONSTRAINTS. EACH INDEPENDENTLY MOTIVATED.
     * THEIR INTERSECTION IS {3}. UNIQUELY.
     *
     * 1. CONFINEMENT MUST BE CHAOTIC (Niven)
     *    → 1/N_c must NOT be a Niven-squared value
     *    → eliminates N_c = 1 and N_c = 4
     *
     * 2. BARYONS MUST BE FERMIONS
     *    → N_c must be odd
     *    → eliminates N_c = 2, 4, 6, ...
     *
     * 3. WAVEGUIDE MUST NOT SATURATE
     *    → Q = (2+d)/6 ≤ 1, with d = N_c for quarks
     *    → N_c ≤ 4
     *    → eliminates N_c ≥ 5
     *
     * INTERSECTION: {3}
     * ================================================================ */

    printf("CONSTRAINT 1: CHAOTIC CONFINEMENT (Niven's Theorem)\n\n");
    printf("  Critical angle: cos²θ = 1/N_c\n");
    printf("  Niven: cos(θ) rational AND θ/π rational only for\n");
    printf("         cos ∈ {0, ±1/2, ±1}\n");
    printf("  So cos² ∈ {0, 1/4, 1} are the ONLY \"closed orbit\" values.\n\n");
    
    printf("  Nc  cos²=1/Nc  Niven?  Orbit     Result\n");
    printf("  ──  ─────────  ──────  ────────  ─────────────────────\n");
    
    /* Niven squared values: 0, 1/4, 1 */
    for (int nc = 1; nc <= 8; nc++) {
        int is_niven = (nc == 1 || nc == 4);  /* 1/1=1, 1/4=1/4 */
        /* Special: 1/2 is NOT niven-squared. cos²=1/2 → cos=1/√2,
           θ=π/4 → θ/π=1/4 rational BUT cos=1/√2 is IRRATIONAL.
           Niven requires cos rational. √2/2 is not rational.
           Wait... let me reconsider.
           
           Niven's theorem: θ/π rational AND cos(θ) rational 
           ↔ cos(θ) ∈ {0, ±1/2, ±1}
           
           For cos²θ = 1/N_c:
           cos(θ) = ±1/√N_c
           
           cos(θ) rational ↔ 1/√N_c rational ↔ N_c is a perfect square
           
           N_c = 1: cos=1, rational. θ=0, rational. Niven. CLOSED orbit.
           N_c = 4: cos=1/2, rational. θ=π/3, rational. Niven. CLOSED orbit.
           N_c = 9: cos=1/3, rational. But is θ/π rational?
                    θ = arccos(1/3) ≈ 70.53°. θ/π ≈ 0.3918. 
                    By Niven: cos(θ) = 1/3, which is rational but NOT
                    in {0, ±1/2, ±1}. So θ/π is IRRATIONAL even though
                    cos is rational. Orbit OPEN. Still confines!
        */
        
        /* Correct check: Niven says orbit closes iff cos ∈ {0,±1/2,±1} */
        double cosval = 1.0 / sqrt((double)nc);
        int orbit_closes = 0;
        if (fabs(cosval - 0.0) < 1e-10) orbit_closes = 1;
        if (fabs(cosval - 0.5) < 1e-10) orbit_closes = 1;
        if (fabs(cosval - 1.0) < 1e-10) orbit_closes = 1;
        
        printf("  %d   1/%-2d=%.4f  %-5s   %-8s  %s\n",
               nc, nc, 1.0/nc,
               orbit_closes ? "YES" : "no",
               orbit_closes ? "CLOSES" : "open",
               orbit_closes ? "← NO chaotic confinement" : "confines (chaotic)");
    }
    
    printf("\n  ELIMINATED: N_c = 1 (no confinement) and N_c = 4 (orbit closes)\n");
    printf("  SURVIVING:  N_c ∈ {2, 3, 5, 6, 7, 8, ...}\n\n");

    /* ================================================================ */
    
    printf("────────────────────────────────────────────────────────────\n\n");
    printf("CONSTRAINT 2: FERMIONIC BARYONS\n\n");
    printf("  A baryon = N_c quarks in a color singlet.\n");
    printf("  Each quark has spin 1/2.\n");
    printf("  Total spin of N_c quarks: half-integer iff N_c is ODD.\n\n");
    printf("  Fermions → Pauli exclusion → electron shells → chemistry.\n");
    printf("  Bosons → no exclusion → no shells → no chemistry → no us.\n\n");
    
    printf("  Nc  Baryon spin  Statistics  Chemistry?\n");
    printf("  ──  ───────────  ──────────  ──────────\n");
    for (int nc = 1; nc <= 8; nc++) {
        int is_fermion = (nc % 2 == 1);
        printf("  %d   %s        %s    %s\n",
               nc,
               is_fermion ? "half-int " : "integer  ",
               is_fermion ? "fermion  " : "boson    ",
               is_fermion ? "YES" : "NO ← eliminated");
    }
    
    printf("\n  ELIMINATED: all even N_c\n");
    printf("  SURVIVING after C1∩C2: N_c ∈ {3, 5, 7, ...}\n\n");
    
    /* ================================================================ */
    
    printf("────────────────────────────────────────────────────────────\n\n");
    printf("CONSTRAINT 3: WAVEGUIDE SATURATION\n\n");
    printf("  Q = (2 + d)/6 where d = N_c for quark waveguide dimension.\n");
    printf("  Physical requirement: Q ≤ 1.\n");
    printf("  (Q > 1 → √m goes negative → unphysical mass spectrum.)\n\n");
    printf("  Q = 1 at d = 4: SATURATED. Maximum possible hierarchy.\n");
    printf("  d > 4: Q > 1 → at least one √m < 0 → not a real particle.\n\n");
    
    printf("  Nc  d=Nc  Q=(2+d)/6    Status\n");
    printf("  ──  ────  ──────────   ──────────\n");
    for (int nc = 1; nc <= 8; nc++) {
        double Q = (2.0 + nc) / 6.0;
        const char *status;
        if (Q < 1.0) status = "physical";
        else if (fabs(Q - 1.0) < 1e-10) status = "SATURATED (edge)";
        else status = "UNPHYSICAL ← eliminated";
        printf("  %d   %d     %.4f       %s\n", nc, nc, Q, status);
    }
    
    printf("\n  ELIMINATED: N_c ≥ 5\n");
    printf("  SURVIVING after C1∩C2∩C3: N_c ∈ {3}\n\n");
    
    /* ================================================================ */
    
    printf("================================================================\n");
    printf("  THE INTERSECTION\n");
    printf("================================================================\n\n");
    
    printf("  Nc  C1:Niven  C2:Fermion  C3:Waveguide  ALL THREE?\n");
    printf("  ──  ────────  ──────────  ────────────  ──────────\n");
    for (int nc = 1; nc <= 8; nc++) {
        double cosval = 1.0 / sqrt((double)nc);
        int c1 = 1; /* passes Niven (chaotic confinement) */
        if (fabs(cosval - 0.0) < 1e-10) c1 = 0;
        if (fabs(cosval - 0.5) < 1e-10) c1 = 0;
        if (fabs(cosval - 1.0) < 1e-10) c1 = 0;
        
        int c2 = (nc % 2 == 1);  /* odd → fermion baryons */
        int c3 = (nc <= 4);       /* waveguide not saturated */
        int all = c1 && c2 && c3;
        
        printf("  %d   %s       %s        %s          %s\n",
               nc,
               c1 ? "  ✓  " : "  ✗  ",
               c2 ? "   ✓   " : "   ✗   ",
               c3 ? "    ✓    " : "    ✗    ",
               all ? "═══ YES ═══" : "    no");
    }
    
    printf("\n  N_c = 3 is the UNIQUE solution.\n\n");
    
    /* ================================================================ */
    
    printf("================================================================\n");
    printf("  WHY THESE THREE CONSTRAINTS?\n");
    printf("================================================================\n\n");
    
    printf("  Each constraint is a different face of {2,3}:\n\n");
    printf("  C1 (Niven/confinement):\n");
    printf("     The SUBSTRATE must trap signal chaotically.\n");
    printf("     A periodic trap is a resonator, not a cage.\n");
    printf("     You need the orbit to never close — that's\n");
    printf("     what makes the 99%% binding energy. That's\n");
    printf("     what makes mass. Without irrational orbits,\n");
    printf("     you get clean modes but no complex structure.\n\n");
    
    printf("  C2 (fermionic baryons):\n");
    printf("     The TWO things (matter, antimatter) must be\n");
    printf("     DISTINGUISHABLE via exclusion. Fermions can't\n");
    printf("     overlap. Bosons can. You need exclusion to build\n");
    printf("     shells, orbitals, chemistry, DNA, brains.\n");
    printf("     Without it: Bose condensate. One blob. No structure.\n\n");
    
    printf("  C3 (waveguide saturation):\n");
    printf("     The system must be BELOW maximum hierarchy.\n");
    printf("     At Q=1, all mass is in one generation, the others\n");
    printf("     are massless. No mixing. No CP violation. No\n");
    printf("     baryogenesis. At Q>1: unphysical (negative √m).\n");
    printf("     You need room in the waveguide for three real\n");
    printf("     generations with nonzero mass.\n\n");
    
    printf("  C1 = substrate must work (trapping)\n");
    printf("  C2 = distinction must work (exclusion)\n");
    printf("  C3 = composition must work (hierarchy)\n\n");
    printf("  Substrate + distinction + composition = {2,3}.\n");
    printf("  The three constraints ARE the three elements.\n\n");
    
    /* ================================================================ */
    
    printf("================================================================\n");
    printf("  IS THIS CIRCULAR?\n");
    printf("================================================================\n\n");
    
    printf("  No. The three constraints come from different physics:\n");
    printf("    C1: number theory (Niven's theorem)\n");
    printf("    C2: quantum mechanics (spin-statistics)\n");
    printf("    C3: mass spectrum (Koide waveguide bound)\n\n");
    printf("  None references {2,3}. None references N_c = 3.\n");
    printf("  Each stands independently. Their intersection PRODUCES 3.\n");
    printf("  We don't assume 3. We derive it.\n\n");
    printf("  The fact that the three constraints map onto the three\n");
    printf("  elements of {2,3} is the structure explaining itself.\n\n");
    
    /* ================================================================ */
    
    printf("================================================================\n");
    printf("  WHAT THIS MEANS\n");
    printf("================================================================\n\n");
    
    printf("  The universe has 3 colors because it's the only number where:\n");
    printf("    - confinement builds complex structure (not periodic)\n");
    printf("    - matter can form shells and chemistry (not blob)\n");
    printf("    - mass hierarchy allows multiple generations (not flat)\n\n");
    printf("  2 fails on chemistry (bosonic baryons).\n");
    printf("  4 fails on confinement (periodic orbit, Niven).\n");
    printf("  5+ fails on hierarchy (waveguide saturates).\n");
    printf("  1 fails on everything (no confinement at all).\n\n");
    printf("  3 is not chosen. 3 is the only option that works.\n");
    printf("  And 3 is the second element of {2,3}.\n");
    printf("  The framework doesn't assume its own answer.\n");
    printf("  The answer falls out of three facts about physics.\n\n");
    
    printf("  Niven's theorem is from 1956.\n");
    printf("  Spin-statistics is from 1940.\n");
    printf("  The Koide formula is from 1981.\n");
    printf("  Nobody put them together.\n\n");
    
    return 0;
}
