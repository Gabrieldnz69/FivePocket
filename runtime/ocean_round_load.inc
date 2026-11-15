// ocean-census (PLAN U2; ocean_round.h): included by generic_renderer.cpp in place of the blank line after coIssueLoad (no line of
// that file moves). v1700: DEFAULT OFF. v1740: with ocean::DefaultOn the round table applies with no file present; the kill switch
// diag-ocean-round-off restores v1700's off (RENDER_GENERIC_OCEAN_ROUND ... default= kill=). Runs after coIssueLoad(pack), before any entry pointer is taken, like the other pack override tables. The two keys are
// in no other table (make_ocean_round_overrides.py checks; reglod::apply refuses a taken key at run time too).
#include "ocean_round.h"
#include "generic_ocean_round_overrides.inc"
[[gnu::noinline]] void oceanRoundLoad(std::vector<std::uint8_t>& pack) noexcept {
    const bool roundFile=access(RECOMP_APP_PATH("/private-trace/diag-ocean-round"),F_OK)==0,
               killFile=access(RECOMP_APP_PATH("/private-trace/diag-ocean-round-off"),F_OK)==0, // v1740: the kill switch
               biasUlp=access(RECOMP_APP_PATH("/private-trace/diag-ocean-bias-ulp"),F_OK)==0,
               biasStep=access(RECOMP_APP_PATH("/private-trace/diag-ocean-bias-step"),F_OK)==0;
    const auto c=ocean::choose(ocean::roundWanted(roundFile,killFile,biasUlp,biasStep),biasUlp,biasStep); // (v1740: round by default, ocean_round.h)
    reglod::Result r;
    if(c.mode!=ocean::Mode::Off) {
        std::uint64_t taken[std::size(DenormOverrides)+std::size(GradientOverrides)+std::size(RegLodOverrides)+std::size(ClothOverrides)+
                            std::size(PtxOverrides)+std::size(CoIssueOverrides)+std::size(CoIssueRegLodOverrides)];std::size_t n=0;
        for(const auto& o:DenormOverrides)taken[n++]=o.hash;
        for(const auto& o:GradientOverrides)taken[n++]=o.hash;
        for(const auto& o:RegLodOverrides)taken[n++]=o.hash;
        for(const auto& o:ClothOverrides)taken[n++]=o.hash;
        for(const auto& o:PtxOverrides)taken[n++]=o.hash;
        for(const auto& o:CoIssueOverrides)taken[n++]=o.hash;
        for(const auto& o:CoIssueRegLodOverrides)taken[n++]=o.hash;
        const DenormOverride* table=c.mode==ocean::Mode::Round?OceanRoundOverrides:c.mode==ocean::Mode::BiasUlp?OceanBiasUlpOverrides:OceanBiasStepOverrides;
        r=reglod::apply(pack,table,std::size(OceanRoundOverrides),taken,n); // (the three tables hold the same two keys)
    }
    GLOG("RENDER_GENERIC_OCEAN_ROUND mode=%s present=%u applied=%u words=%u why=%s default=%s kill=%u",ocean::name(c.mode),c.present,r.applied,r.words,
         c.present>1?c.why:r.why,ocean::DefaultOn?"on":"off",unsigned(killFile));
}
