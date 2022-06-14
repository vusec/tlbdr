use crate::state::*;
use crate::state::hier::*;
use crate::policy::*;
use crate::policy::hier::*;

pub trait Preset {
    type State: CacheState;
    type RP: CacheRP<State=Self::State>;

    fn rp(&self) -> Self::RP;
    fn rpx(&self) -> Option<Self::RP> { None }
    fn newstate(&self) -> Self::State;
}

pub enum TLB { IVYBRIDGE, HASWELL, KABYLAKE }
impl TLB {
    const RP_IVY: H2SRP<PVRP, PVRP, PVRP> = H2SRP {
        rp1i: PVRP::PLRU4, rp1d: PVRP::PLRU4, rp2: PVRP::PLRU4, rxmem: false,
        miss_l1i: true, miss_l1d: true, miss_l2: true,
        prop_upi: true, prop_upd: true, prop_dn: false
    };
    const RP_HASWELL: H2SRP<PVRP, PVRP, PVRP> = H2SRP {
        rp1i: PVRP::PLRU8, rp1d: PVRP::PLRU4, rp2: PVRP::PLRU8, rxmem: false,
        miss_l1i: true, miss_l1d: true, miss_l2: true,
        prop_upi: true, prop_upd: true, prop_dn: false
    };
    const RP_KABY: H2SRP<PVRP, PVRP, PVRP> = H2SRP {
        rp1i: PVRP::PLRU8, rp1d: PVRP::PLRU4, rp2: PVRP::MRU3PLRU4, rxmem: false,
        miss_l1i: true, miss_l1d: true, miss_l2: true,
        prop_upi: true, prop_upd: true, prop_dn: false
    };
}
impl Preset for TLB {
    type State = H2SState<PVec, PVec, PVec>;
    type RP = H2SRP<PVRP, PVRP, PVRP>;

    fn rp(&self) -> Self::RP {
        use TLB::*;
        match self {
            IVYBRIDGE => Self::RP_IVY,
            HASWELL => Self::RP_HASWELL,
            KABYLAKE => Self::RP_KABY
        }
    }
    fn rpx(&self) -> Option<Self::RP> {
        Some(H2SRP{rxmem: true, ..self.rp()})
    }
    fn newstate(&self) -> Self::State {
        let rp = self.rp();
        H2SState{ l1i: rp.rp1i.newpv(), l1d: rp.rp1d.newpv(), l2: rp.rp2.newpv()}
    }
}

pub mod dcache {
    use crate::state::*;
    use crate::state::hier::*;
    use crate::policy::*;
    use crate::policy::hier::*;
    use crate::preset::Preset;

    pub struct NEHALEM;
    impl Preset for NEHALEM {
        type State = H3UState<PVec, PVec, QVec>;
        type RP = H3URP<PVRP, PVRP, QLRU>;

        fn rp(&self) -> Self::RP {
            H3URP {
                rp1: PVRP::PLRU8, rp2: PVRP::PLRU8,
                rp3: QLRU{h: [0, 0, 0, 0], m: 0, r: 0, u: 1, umo: false},
                miss_l1: true, miss_l2: true, miss_l3: true,
                prop_up: true, prop_dn: false
            }
        }
        fn newstate(&self) -> Self::State {
            let rp = self.rp();
            H3UState{l1: rp.rp1.newpv(), l2: rp.rp2.newpv(), l3: rp.rp3.fillup(&QVec::new(16), Entry::X)}
        }
    }

    pub struct HASWELL;
    impl Preset for HASWELL {
        type State = H3UState<PVec, PVec, QVec>;
        type RP = H3URP<PVRP, PVRP, QLRU>;

        fn rp(&self) -> Self::RP {
            H3URP {
                rp1: PVRP::PLRU8, rp2: PVRP::PLRU8,
                rp3: QLRU{h: [0, 0, 1, 1], m: 1, r: 0, u: 0, umo: false},
                miss_l1: true, miss_l2: true, miss_l3: true,
                prop_up: true, prop_dn: false
            }
        }
        fn newstate(&self) -> Self::State {
            let rp = self.rp();
            H3UState{l1: rp.rp1.newpv(), l2: rp.rp2.newpv(), l3: rp.rp3.fillup(&QVec::new(16), Entry::X)}
        }
    }

    pub struct KABYLAKE;
    impl Preset for KABYLAKE {
        type State = H3UState<PVec, QVec, QVec>;
        type RP = H3URP<PVRP, QLRU, QLRU>;

        fn rp(&self) -> Self::RP {
            H3URP {
                rp1: PVRP::PLRU8, rp2: QLRU{h: [0, 0, 0, 0], m: 1, r: 2, u: 1, umo: false},
                rp3: QLRU{h: [0, 0, 1, 1], m: 1, r: 0, u: 0, umo: false},
                miss_l1: true, miss_l2: true, miss_l3: true,
                prop_up: true, prop_dn: false
            }
        }
        fn newstate(&self) -> Self::State {
            let rp = self.rp();
            H3UState{l1: rp.rp1.newpv(), l2: rp.rp2.fillup(&QVec::new(4), Entry::X), l3: rp.rp3.fillup(&QVec::new(16), Entry::X)}
        }
    }
}
