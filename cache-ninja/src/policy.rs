use std::hash::Hash;
use crate::state::*;

#[derive(PartialEq, Eq, Hash, Copy, Clone, Default, Debug)]
pub struct Origin {
    pub isnfetch: bool
}

#[derive(PartialEq, Eq, Hash, Copy, Clone, Debug)]
pub enum Access {
    Hit(u8, Entry, Origin),
    Miss(Entry, Origin)
}

pub trait CacheRP: std::fmt::Debug {
    type State: CacheState;
    type EVict: std::fmt::Debug;

    const HIT_COST: usize = 1;
    const MISS_COST: usize = 2;

    fn heur(&self, st: &Self::State, val: Entry) -> usize;
    fn evictim(&self, st: &Self::State) -> Self::EVict;
    fn evictim1(&self, st: &Self::State) -> Entry;
    fn update(&self, st: &Self::State, val: Entry, orig: Origin) -> (Self::State, Access);
    fn update_shallow(&self, st: &Self::State, val: Entry, orig: Origin) -> (Self::State, Access);

    fn update_def(&self, st: &Self::State, val: Entry) -> (Self::State, Access) {
        self.update(st, val, Default::default())
    }
    fn outedges_priced(&self, st: &Self::State) -> (Vec<Self::State>, Vec<Access>, Vec<usize>) {
        use std::iter::{once, repeat};

        let (vst, vacc): (Vec<Self::State>, Vec<Access>) = if cfg!(feature = "nohit") {
            once(self.update(st, Entry::P(st.next_free_paddr(), Default::default()), Default::default())).unzip()
        } else {
            st.entries().filter_map(|x| match x {
                Entry::P(_,_) => Some(self.update(st, x, Default::default())),
                _ => None
            }).chain(once(self.update(st, Entry::P(st.next_free_paddr(), Default::default()), Default::default()))
            ).unzip()
        };
        let costs = if cfg!(feature = "nohit") {
            vec![Self::MISS_COST]
        } else {
            repeat(Self::HIT_COST).take(vst.len() - 1).chain(once(Self::MISS_COST)).collect()
        };
        (vst, vacc, costs)
    }
    fn outedges(&self, st: &Self::State) -> (Vec<Self::State>, Vec<Access>) {
        let (sts, acc, _costs) = self.outedges_priced(st);
        (sts, acc)
    }
    fn successors_priced(&self, st: &Self::State) -> Vec<(Self::State, usize)> {
        let (sts, _acc, costs) = self.outedges_priced(st);
        sts.into_iter().zip(costs).collect()
    }
    fn successors(&self, st: &Self::State) -> Vec<Self::State> {
        self.outedges_priced(st).0
    }
    fn find_outedge(&self, st: &Self::State, nx: &Self::State) -> Option<Access> {
        let (s, e) = self.outedges(st);
        if let Some(i) = s.iter().position(|x| x == nx) {
            Some(e[i])
        } else {
            None
        }
    }
}


#[derive(PartialEq, Eq, Hash, Copy, Clone, Debug)]
pub enum PVRP { PLRU4, PLRU8, PLRU16, LRU3PLRU4, MRU3PLRU4 }
impl PVRP {
    const PI_PLRU4: [[u8; 4]; 5] = [
        [0, 1, 2, 3],
        [1, 0, 3, 2],
        [2, 1, 0, 3],
        [3, 0, 1, 2],
        [3, 0, 1, 2]
    ];
    const HF_PLRU4: [u8; 4] = [3, 3, 2, 1];
    const PI_PLRU8: [[u8; 8]; 9] = [
        [0, 1, 2, 3, 4, 5, 6, 7],
        [1, 0, 3, 2, 5, 4, 7, 6],
        [2, 1, 0, 3, 6, 5, 4, 7],
        [3, 0, 1, 2, 7, 4, 5, 6],
        [4, 1, 2, 3, 0, 5, 6, 7],
        [5, 0, 3, 2, 1, 4, 7, 6],
        [6, 1, 0, 3, 2, 5, 4, 7],
        [7, 0, 1, 2, 3, 4, 5, 6],
        [7, 0, 1, 2, 3, 4, 5, 6]
    ];
    const HF_PLRU8: [u8; 8] = [4, 4, 4, 4, 3, 3, 2, 1];
    const PI_PLRU16: [[u8; 16]; 17] = [
        [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15],
        [1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14],
        [2, 1, 0, 3, 6, 5, 4, 7, 10, 9, 8, 11, 14, 13, 12, 15],
        [3, 0, 1, 2, 7, 4, 5, 6, 11, 8, 9, 10, 15, 12, 13, 14],
        [4, 1, 2, 3, 0, 5, 6, 7, 12, 9, 10, 11, 8, 13, 14, 15],
        [5, 0, 3, 2, 1, 4, 7, 6, 13, 8, 11, 10, 9, 12, 15, 14],
        [6, 1, 0, 3, 2, 5, 4, 7, 14, 9, 8, 11, 10, 13, 12, 15],
        [7, 0, 1, 2, 3, 4, 5, 6, 15, 8, 9, 10, 11, 12, 13, 14],
        [8, 1, 2, 3, 4, 5, 6, 7, 0, 9, 10, 11, 12, 13, 14, 15],
        [9, 0, 3, 2, 5, 4, 7, 6, 1, 8, 11, 10, 13, 12, 15, 14],
        [10, 1, 0, 3, 6, 5, 4, 7, 2, 9, 8, 11, 14, 13, 12, 15],
        [11, 0, 1, 2, 7, 4, 5, 6, 3, 8, 9, 10, 15, 12, 13, 14],
        [12, 1, 2, 3, 0, 5, 6, 7, 4, 9, 10, 11, 8, 13, 14, 15],
        [13, 0, 3, 2, 1, 4, 7, 6, 5, 8, 11, 10, 9, 12, 15, 14],
        [14, 1 ,0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15],
        [15, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14],
        [15, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14],
    ];
    const HF_PLRU16: [u8; 16] = [5, 5, 5, 5, 5, 5, 5, 5, 4, 4, 4, 4, 3, 3, 2, 1];
    const PI_LRU3PLRU4: [[u8; 12]; 13] = [
        [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11],
        [1, 0, 2, 4, 3, 5, 7, 6, 8, 10, 9, 11],
        [2, 0, 1, 5, 3, 4, 8, 6, 7, 11, 9, 10],
        [3, 1, 2, 0, 4, 5, 9, 7, 8, 6, 10, 11],
        [4, 0, 2, 1, 3, 5, 10, 6, 8, 7, 9, 11],
        [5, 0, 1, 2, 3, 4, 11, 6, 7, 8, 9, 10],
        [6, 1, 2, 3, 4, 5, 0, 7, 8, 9, 10, 11],
        [7, 0, 2, 4, 3, 5, 1, 6, 8, 10, 9, 11],
        [8, 0, 1, 5, 3, 4, 2, 6, 7, 11, 9, 10],
        [9, 1, 2, 0, 4, 5, 3, 7, 8, 6, 10, 11],
        [10, 0, 2, 1, 3, 5, 4, 6, 8, 7, 9, 11],
        [11, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10],
        [11, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
    ];
    const HF_LRU3PLRU4: [u8; 12] = [5, 5, 5, 5, 5, 5, 4, 4, 4, 3, 2, 1];
    const PI_MRU3PLRU4: [[u8; 12]; 13] = [
        [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11],
        [1, 2, 0, 4, 5, 3, 7, 8, 6, 10, 11, 9],
        [2, 0, 1, 5, 3, 4, 8, 6, 7, 11, 9, 10],
        [3, 1, 2, 0, 4, 5, 9, 7, 8, 6, 10, 11],
        [4, 2, 0, 1, 5, 3, 10, 8, 6, 7, 11, 9],
        [5, 0, 1, 2, 3, 4, 11, 6, 7, 8, 9, 10],
        [6, 1, 2, 3, 4, 5, 0, 7, 8, 9, 10, 11],
        [7, 2, 0, 4, 5, 3, 1, 8, 6, 10, 11, 9],
        [8, 0, 1, 5, 3, 4, 2, 6, 7, 11, 9, 10],
        [9, 1, 2, 0, 4, 5, 3, 7, 8, 6, 10, 11],
        [10, 2, 0, 1, 5, 3, 4, 8, 6, 7, 11, 9],
        [11, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10],
        [11, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
    ];
    const HF_MRU3PLRU4: [u8; 12] = [4, 4, 4, 4, 4, 4, 3, 3, 3, 2, 2, 1];

    const fn pilen(&self) -> usize {
        use PVRP::*;
        match self {
            PLRU4 => 4,
            PLRU8 => 8,
            PLRU16 => 16,
            LRU3PLRU4 |
            MRU3PLRU4 => 12
        }
    }
    const fn pi(&self, i: usize) -> &'static [u8] {
        use PVRP::*;
        match self {
            PLRU4 => &Self::PI_PLRU4[i],
            PLRU8 => &Self::PI_PLRU8[i],
            PLRU16 => &Self::PI_PLRU16[i],
            LRU3PLRU4 => &Self::PI_LRU3PLRU4[i],
            MRU3PLRU4 => &Self::PI_MRU3PLRU4[i]
        }
    }
    const fn hf(&self, i: usize) -> usize {
        use PVRP::*;
        (match self {
            PLRU4 => Self::HF_PLRU4[i],
            PLRU8 => Self::HF_PLRU8[i],
            PLRU16 => Self::HF_PLRU16[i],
            LRU3PLRU4 => Self::HF_LRU3PLRU4[i],
            MRU3PLRU4 => Self::HF_MRU3PLRU4[i]
        }) as usize
    }

    pub fn newpv(&self) -> PVec {
        PVec::new(self.pilen())
    }
    fn permute(&self, pv: &Vec<Entry>, i: usize) -> Vec<Entry> {
        self.pi(i).iter().map(|&x| pv[x as usize]).collect()
    }
}

impl CacheRP for PVRP {
    type State = PVec;
    type EVict = Entry;

    fn heur(&self, st: &PVec, val: Entry) -> usize {
        match st.rank(val) {
            Some(i) => self.hf(i) - Self::HIT_COST + Self::MISS_COST,
            None => 0
        }
    }
    fn evictim(&self, st: &PVec) -> Entry {
        st.0[st.0.len()-1]
    }
    fn evictim1(&self, st: &PVec) -> Entry {
        self.evictim(st)
    }
    fn update(&self, st: &PVec, val: Entry, orig: Origin) -> (PVec, Access) {
        match st.rank(val) {
            Some(i) => (PVec(self.permute(&st.0, i)), Access::Hit(0, val, orig)),
            None => {
                let mut nv = self.permute(&st.0, self.pilen());
                nv[0] = val;
                (PVec(nv), Access::Miss(val, orig))
            }
        }
    }
    fn update_shallow(&self, st: &PVec, val: Entry, orig: Origin) -> (PVec, Access) {
        self.update(st, val, orig)
    }
}


#[derive(Debug)]
pub struct QLRU {
    pub h: [u8; 4],
    pub m: u8,
    pub r: u8,
    pub u: u8,
    pub umo: bool
}

impl QLRU {
    fn ageup(&self, av: &mut Vec<u8>, trigi: usize) {
        let maxage = av.iter().copied().max().unwrap_or_default();
        let incby = if self.u & 2 != 0 { 1 } else { 3 - maxage };
        if maxage < 3 {
            let prev = av[trigi];
            for age in av.iter_mut() {
                *age += incby;
            }
            if self.u & 1 != 0 {
                av[trigi] = prev;
            }
        }
    }
    fn handle_hit(&self, st: &QVec, i: usize) -> QVec {
        let mut nav = st.age.to_vec();
        nav[i] = self.h[st.age[i] as usize];
        if !self.umo {
            self.ageup(&mut nav, i);
        }
        QVec{age: nav, ent: st.ent.to_vec()}
    }
    fn handle_miss(&self, st: &QVec, val: Entry) -> QVec {
        let evi = st.evictim().unwrap_or(if self.r & 2 != 0 { st.age.len() - 1 } else { 0 });
        let mut nev = st.ent.to_vec();
        let mut nav = st.age.to_vec();
        nev[evi] = val;
        nav[evi] = self.m;
        self.ageup(&mut nav, evi);
        QVec{age: nav, ent: nev}
    }
    pub fn fillup(&self, st: &QVec, val: Entry) -> QVec {
        let mut s = self.handle_miss(st, val);
        for _ in 1..st.age.len() {
            s = self.handle_miss(&s, val);
        }
        s
    }
}

impl CacheRP for QLRU {
    type State = QVec;
    type EVict = Entry;

    fn heur(&self, st: &QVec, val: Entry) -> usize {
        match st.rank(val) {
            Some((age, i)) => 1 +
                st.age.iter().map(|x| if *x > age {1} else {0} as usize).sum::<usize>() +
                st.age.iter().take(i).map(|x| if *x == age {1} else {0}).sum::<usize>(),
            _ => 0
        }
    }
    fn evictim(&self, st: &QVec) -> Entry {
        match st.evictim() {
            Some(i) => st.ent[i],
            None => Entry::X
        }
    }
    fn evictim1(&self, st: &QVec) -> Entry {
        self.evictim(st)
    }
    fn update(&self, st: &QVec, val: Entry, orig: Origin) -> (QVec, Access) {
        match st.rank(val) {
            Some((_, i)) => (self.handle_hit(st, i), Access::Hit(0, val, orig)),
            None => (self.handle_miss(st, val), Access::Miss(val, orig))
        }
    }
    fn update_shallow(&self, st: &QVec, val: Entry, orig: Origin) -> (QVec, Access) {
        self.update(st, val, orig)
    }
}


pub mod hier {
    use crate::state::*;
    use crate::state::hier::*;
    use crate::policy::*;

    #[derive(Debug)]
    pub struct H2URP<RP1: CacheRP<EVict=Entry>, RP2: CacheRP<EVict=Entry>> {
        pub rp1: RP1,
        pub rp2: RP2,
        pub miss_l1: bool,
        pub miss_l2: bool,
        pub prop_up: bool,
        pub prop_dn: bool,
        //llvict: bool // TODO: impl
    }

    impl<RP1, RP2> CacheRP for H2URP<RP1, RP2>
    where RP1: CacheRP<EVict=Entry>, RP2: CacheRP<EVict=Entry>
    {
        type State = H2UState<RP1::State, RP2::State>;
        type EVict = (Entry, Entry);

        fn heur(&self, st: &Self::State, val: Entry) -> usize {
            std::cmp::max(self.rp1.heur(&st.l1, val), self.rp2.heur(&st.l2, val))
        }
        fn evictim(&self, st: &Self::State) -> Self::EVict {
            (self.rp1.evictim(&st.l1), self.rp2.evictim(&st.l2))
        }
        fn evictim1(&self, st: &Self::State) -> Entry {
            self.evictim(st).0
        }
        fn update(&self, st: &Self::State, val: Entry, orig: Origin) -> (Self::State, Access) {
            if st.l1.contains(val) {
                let newl2 = if self.prop_dn { self.rp2.update_def(&st.l2, val).0 } else { st.l2.clone() };
                (H2UState{l1: self.rp1.update_def(&st.l1, val).0, l2: newl2}, Access::Hit(1, val, orig))
            } else if st.l2.contains(val) {
                let newl1 = if self.prop_up { self.rp1.update_def(&st.l1, val).0 } else { st.l1.clone() };
                (H2UState{l1: newl1, l2: self.rp2.update_def(&st.l2, val).0}, Access::Hit(2, val, orig))
            } else {
                (H2UState{
                    l1: if self.miss_l1 { self.rp1.update_def(&st.l1, val).0 } else { st.l1.clone() },
                    l2: if self.miss_l2 { self.rp2.update_def(&st.l2, val).0 } else { st.l2.clone() }
                },
                Access::Miss(val, orig))
            }
        }
        fn update_shallow(&self, st: &Self::State, val: Entry, orig: Origin) -> (Self::State, Access) {
            (H2UState{l1: self.rp1.update_def(&st.l1, val).0, l2: st.l2.clone()},
             if st.l1.contains(val) { Access::Hit(1, val, orig) } else { Access::Miss(val, orig) })
        }
    }


    #[derive(Debug)]
    pub struct H3URP<RP1, RP2, RP3>
    where RP1: CacheRP<EVict=Entry>, RP2: CacheRP<EVict=Entry>, RP3: CacheRP<EVict=Entry>
    {
        pub rp1: RP1,
        pub rp2: RP2,
        pub rp3: RP3,
        pub miss_l1: bool,
        pub miss_l2: bool,
        pub miss_l3: bool,
        pub prop_up: bool,
        pub prop_dn: bool,
        //llvict: bool // TODO: impl
    }

    impl<RP1, RP2, RP3> CacheRP for H3URP<RP1, RP2, RP3>
    where RP1: CacheRP<EVict=Entry>, RP2: CacheRP<EVict=Entry>, RP3: CacheRP<EVict=Entry>
    {
        type State = H3UState<RP1::State, RP2::State, RP3::State>;
        type EVict = (Entry, Entry, Entry);

        fn heur(&self, st: &Self::State, val: Entry) -> usize {
            use std::cmp::max;
            max(max(self.rp1.heur(&st.l1, val), self.rp2.heur(&st.l2, val)), self.rp3.heur(&st.l3, val))
        }
        fn evictim(&self, st: &Self::State) -> Self::EVict {
            (self.rp1.evictim(&st.l1), self.rp2.evictim(&st.l2), self.rp3.evictim(&st.l3))
        }
        fn evictim1(&self, st: &Self::State) -> Entry {
            self.evictim(st).0
        }
        fn update(&self, st: &Self::State, val: Entry, orig: Origin) -> (Self::State, Access) {
            if st.l1.contains(val) {
                let (newl2, newl3) = if self.prop_dn {
                    (self.rp2.update_def(&st.l2, val).0, self.rp3.update_def(&st.l3, val).0)
                } else {
                    (st.l2.clone(), st.l3.clone())
                };
                (H3UState{l1: self.rp1.update_def(&st.l1, val).0, l2: newl2, l3: newl3}, Access::Hit(1, val, orig))
            } else if st.l2.contains(val) {
                let newl1 = if self.prop_up { self.rp1.update_def(&st.l1, val).0 } else { st.l1.clone() };
                let newl3 = if self.prop_dn { self.rp3.update_def(&st.l3, val).0 } else { st.l3.clone() };
                (H3UState{l1: newl1, l2: self.rp2.update_def(&st.l2, val).0, l3: newl3}, Access::Hit(2, val, orig))
            } else if st.l3.contains(val) {
                let (newl1, newl2) = if self.prop_up {
                    (self.rp1.update_def(&st.l1, val).0, self.rp2.update_def(&st.l2, val).0)
                } else {
                    (st.l1.clone(), st.l2.clone())
                };
                (H3UState{l1: newl1, l2: newl2, l3: self.rp3.update_def(&st.l3, val).0}, Access::Hit(3, val, orig))
            } else {
                (H3UState{
                    l1: if self.miss_l1 { self.rp1.update_def(&st.l1, val).0 } else { st.l1.clone() },
                    l2: if self.miss_l2 { self.rp2.update_def(&st.l2, val).0 } else { st.l2.clone() },
                    l3: if self.miss_l3 { self.rp3.update_def(&st.l3, val).0 } else { st.l3.clone() }
                },
                Access::Miss(val, orig))
            }
        }
        fn update_shallow(&self, st: &Self::State, val: Entry, orig: Origin) -> (Self::State, Access) {
            (H3UState{l1: self.rp1.update_def(&st.l1, val).0, l2: st.l2.clone(), l3: st.l3.clone()},
             if st.l1.contains(val) { Access::Hit(1, val, orig) } else { Access::Miss(val, orig) })
        }
    }


    #[derive(Debug)]
    pub struct H2SRP<RP1I: CacheRP, RP1D: CacheRP, RP2: CacheRP> {
        pub rp1i: RP1I,
        pub rp1d: RP1D,
        pub rp2: RP2,
        pub rxmem: bool,
        pub miss_l1i: bool,
        pub miss_l1d: bool,
        pub miss_l2: bool,
        pub prop_upi: bool,
        pub prop_upd: bool,
        pub prop_dn: bool,
        //llvicti: bool, // TODO: impl
        //llvictd: bool  // TODO: impl
    }

    impl<RP1I, RP1D, RP2> CacheRP for H2SRP<RP1I, RP1D, RP2>
    where RP1I: CacheRP<EVict=Entry>, RP1D: CacheRP<EVict=Entry>, RP2: CacheRP<EVict=Entry>
    {
        type State = H2SState<RP1I::State, RP1D::State, RP2::State>;
        type EVict = (Entry, Entry, Entry);

        fn heur(&self, st: &Self::State, val: Entry) -> usize {
            use std::cmp::max;
            max(max(self.rp1i.heur(&st.l1i, val), self.rp1d.heur(&st.l1d, val)), self.rp2.heur(&st.l2, val))
        }
        fn evictim(&self, st: &Self::State) -> Self::EVict {
            (self.rp1i.evictim(&st.l1i), self.rp1d.evictim(&st.l1d), self.rp2.evictim(&st.l2))
        }
        fn evictim1(&self, st: &Self::State) -> Entry {
            self.evictim(st).1
        }
        fn update(&self, st: &Self::State, val: Entry, orig: Origin) -> (Self::State, Access) {
            if orig.isnfetch {
                if st.l1i.contains(val) {
                    let newl2 = if self.prop_dn { self.rp2.update_def(&st.l2, val).0 } else { st.l2.clone() };
                    (H2SState{l1i: self.rp1i.update_def(&st.l1i, val).0, l1d: st.l1d.clone(), l2: newl2}, Access::Hit(1, val, orig))
                } else if st.l2.contains(val) {
                    let newl1i = if self.prop_upi { self.rp1i.update_def(&st.l1i, val).0 } else { st.l1i.clone() };
                    (H2SState{l1i: newl1i, l1d: st.l1d.clone(), l2: self.rp2.update_def(&st.l2, val).0}, Access::Hit(2, val, orig))
                } else {
                    (H2SState{
                        l1i: if self.miss_l1i { self.rp1i.update_def(&st.l1i, val).0 } else { st.l1i.clone() },
                        l1d: st.l1d.clone(),
                        l2: if self.miss_l2 { self.rp2.update_def(&st.l2, val).0 } else { st.l2.clone() }
                    },
                    Access::Miss(val, orig))
                }
            } else {
                if st.l1d.contains(val) {
                    let newl2 = if self.prop_dn { self.rp2.update_def(&st.l2, val).0 } else { st.l2.clone() };
                    (H2SState{l1i: st.l1i.clone() , l1d: self.rp1d.update_def(&st.l1d, val).0, l2: newl2}, Access::Hit(1, val, orig))
                } else if st.l2.contains(val) {
                    let newl1d = if self.prop_upd { self.rp1d.update_def(&st.l1d, val).0 } else { st.l1d.clone() };
                    (H2SState{l1i: st.l1i.clone(), l1d: newl1d, l2: self.rp2.update_def(&st.l2, val).0}, Access::Hit(2, val, orig))
                } else {
                    (H2SState{
                        l1i: st.l1i.clone(),
                        l1d: if self.miss_l1d { self.rp1d.update_def(&st.l1d, val).0 } else { st.l1d.clone() },
                        l2: if self.miss_l2 { self.rp2.update_def(&st.l2, val).0 } else { st.l2.clone() }
                    },
                    Access::Miss(val, orig))
                }
            }
        }
        fn update_shallow(&self, st: &Self::State, val: Entry, orig: Origin) -> (Self::State, Access) {
            if orig.isnfetch {
                (H2SState{l1i: self.rp1i.update_def(&st.l1i, val).0, l1d: st.l1d.clone(), l2: st.l2.clone()},
                 if st.l1i.contains(val) { Access::Hit(1, val, orig) } else { Access::Miss(val, orig) })
            } else {
                (H2SState{l1i: st.l1i.clone(), l1d: self.rp1d.update_def(&st.l1d, val).0, l2: st.l2.clone()},
                 if st.l1d.contains(val) { Access::Hit(1, val, orig) } else { Access::Miss(val, orig) })
             }
        }
        fn outedges_priced(&self, st: &Self::State) -> (Vec<Self::State>, Vec<Access>, Vec<usize>) {
            const DATACOL: PColor = false;
            const ISNFCOL: PColor = true;
            use std::iter::{once, repeat};
            use Entry::*;

            let l1i = st.l1i.entries().filter_map(|x| match x {
                P(_,_) => Some(self.update(st, x, Origin{isnfetch: true})),
                _ => None
            });
            let l1d = st.l1d.entries().filter_map(|x| match x {
                P(_,_) => Some(self.update(st, x, Origin{isnfetch: false})),
                _ => None
            });
            let l2i = st.l2.entries().filter_map(|x| match x {
                P(_, c) => if self.rxmem || c == ISNFCOL {
                    Some(self.update(st, x, Origin{isnfetch: true}))
                } else { None },
                _ => None
            });
            let l2d = st.l2.entries().filter_map(|x| match x {
                P(_, c) => if !st.l1d.contains(x) && (self.rxmem || c == DATACOL) {
                    Some(self.update(st, x, Origin{isnfetch: false}))
                } else { None },
                _ => None
            });
            let nfp = st.next_free_paddr();
            let imiss = once(self.update(st, P(nfp, ISNFCOL), Origin{isnfetch: true}));
            let dmiss = once(self.update(st, P(nfp, DATACOL), Origin{isnfetch: false}));

            let (vst, vacc): (Vec<Self::State>, Vec<Access>) = if cfg!(feature = "nohit") {
                if cfg!(feature = "isnprio") {
                    imiss.chain(dmiss).unzip()
                } else {
                    dmiss.chain(imiss).unzip()
                }
            } else {
                if cfg!(feature = "isnprio") {
                    if cfg!(feature = "nol1hit") {
                        l2i.chain(l2d).chain(imiss).chain(dmiss).unzip()
                    } else {
                        l1i.chain(l1d).chain(l2i).chain(l2d).chain(imiss).chain(dmiss).unzip()
                    }
                } else {
                    if cfg!(feature = "nol1hit") {
                        l2d.chain(l2i).chain(dmiss).chain(imiss).unzip()
                    } else {
                        l1d.chain(l1i).chain(l2d).chain(l2i).chain(dmiss).chain(imiss).unzip()
                    }
                }
            };
            let costs = repeat(Self::HIT_COST).take(vst.len() - 2).chain(repeat(Self::MISS_COST).take(2)).collect();
            (vst, vacc, costs)
        }
    }
}
