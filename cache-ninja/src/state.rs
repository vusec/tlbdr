use std::hash::Hash;

pub type PAddr = u8;
pub type PColor = bool;


#[derive(PartialEq, Eq, Hash, Copy, Clone, Debug)]
pub enum Entry {
    X,
    T,
    P(PAddr, PColor)
}

pub trait CacheState: Eq + Hash + Clone + std::fmt::Debug {
    type EntryRank;
    type EntryIter: Iterator<Item = Entry>;

    fn rank(&self, val: Entry) -> Option<Self::EntryRank>;
    fn contains(&self, val: Entry) -> bool {
        self.rank(val).is_some()
    }
    fn entries(&self) -> Self::EntryIter;
    fn next_free_paddr(&self) -> PAddr {
        let iset: Vec<PAddr> = self.entries().filter_map(|x| match x {
            Entry::P(a, _) => Some(a),
            _ => None
        }).collect();
        for i in 0..=(iset.len() as PAddr) {
            if !iset.iter().any(|&x| x == i) {
                return i
            }
        }
        panic!("Logic error in next_free_paddr algorithm");
    }
}


#[derive(PartialEq, Eq, Hash, Clone, Debug)]
pub struct PVec(pub Vec<Entry>);

impl PVec {
    pub fn new(sz: usize) -> Self {
        Self(vec![Entry::X; sz])
    }
}

impl<'a> CacheState for &'a PVec {
    type EntryRank = usize;
    type EntryIter = std::iter::Copied<std::slice::Iter<'a, Entry>>;

    fn rank(&self, val: Entry) -> Option<Self::EntryRank> {
        self.0.iter().position(|&x| x == val)
    }
    fn entries(&self) -> Self::EntryIter {
        self.0.iter().copied()
    }
}
impl CacheState for PVec {
    type EntryRank = usize;
    type EntryIter = std::vec::IntoIter<Entry>;

    fn rank(&self, val: Entry) -> Option<Self::EntryRank> {
        (&self).rank(val)
    }
    fn entries(&self) -> Self::EntryIter {
        self.0.to_vec().into_iter()
    }
}


#[derive(PartialEq, Eq, Hash, Clone, Debug)]
pub struct QVec{
    pub age: Vec<u8>,
    pub ent: Vec<Entry>
}

impl QVec {
    pub fn new(sz: usize) -> Self {
        Self{age: vec![3; sz], ent: vec![Entry::X; sz]}
    }
    pub fn evictim(&self) -> Option<usize> {
        self.age.iter().position(|&x| x == 3)
    }
}

impl<'a> CacheState for &'a QVec {
    type EntryRank = (u8, usize);
    type EntryIter = std::iter::Copied<std::slice::Iter<'a, Entry>>;

    fn rank(&self, val: Entry) -> Option<Self::EntryRank> {
        match self.ent.iter().position(|&x| x == val) {
            Some(i) => Some((self.age[i], i)),
            _ => None
        }
    }
    fn entries(&self) -> Self::EntryIter {
        self.ent.iter().copied()
    }
}
impl CacheState for QVec {
    type EntryRank = (u8, usize);
    type EntryIter = std::vec::IntoIter<Entry>;

    fn rank(&self, val: Entry) -> Option<Self::EntryRank> {
        (&self).rank(val)
    }
    fn entries(&self) -> Self::EntryIter {
        self.ent.to_vec().into_iter()
    }
}


pub mod hier {
    use crate::state::*;
    use itertools::Itertools;

    #[derive(PartialEq, Eq, Hash, Clone, Debug)]
    pub struct H2UState<L1T: CacheState, L2T: CacheState>
    {
        pub l1: L1T,
        pub l2: L2T
    }

    impl<L1T, L2T> CacheState for H2UState<L1T, L2T>
    where L1T: CacheState, L2T: CacheState
    {
        type EntryRank = (Option<L1T::EntryRank>, Option<L2T::EntryRank>);
        type EntryIter = itertools::Unique<std::iter::Chain<L1T::EntryIter, L2T::EntryIter>>;

        fn rank(&self, val: Entry) -> Option<Self::EntryRank> {
            match (self.l1.rank(val), self.l2.rank(val)) {
                (None, None) => None,
                (a, b) => Some((a, b))
            }
        }
        fn entries(&self) -> Self::EntryIter {
            self.l1.entries().chain(self.l2.entries()).unique()
        }
    }


    #[derive(PartialEq, Eq, Hash, Clone, Debug)]
    pub struct H3UState<L1T: CacheState, L2T: CacheState, L3T: CacheState>
    {
        pub l1: L1T,
        pub l2: L2T,
        pub l3: L3T
    }

    impl<L1T, L2T, L3T> CacheState for H3UState<L1T, L2T, L3T>
    where L1T: CacheState, L2T: CacheState, L3T: CacheState
    {
        type EntryRank = (Option<L1T::EntryRank>, Option<L2T::EntryRank>, Option<L3T::EntryRank>);
        type EntryIter = itertools::Unique<std::iter::Chain<std::iter::Chain<L1T::EntryIter, L2T::EntryIter>, L3T::EntryIter>>;

        fn rank(&self, val: Entry) -> Option<Self::EntryRank> {
            match (self.l1.rank(val), self.l2.rank(val), self.l3.rank(val)) {
                (None, None, None) => None,
                (a, b, c) => Some((a, b, c))
            }
        }
        fn entries(&self) -> Self::EntryIter {
            self.l1.entries().chain(self.l2.entries()).chain(self.l3.entries()).unique()
        }
    }


    #[derive(PartialEq, Eq, Hash, Clone, Debug)]
    pub struct H2SState<L1IT: CacheState, L1DT: CacheState, L2T: CacheState> {
        pub l1i: L1IT,
        pub l1d: L1DT,
        pub l2: L2T
    }

    impl<L1IT, L1DT, L2T> CacheState for H2SState<L1IT, L1DT, L2T>
    where L1IT: CacheState, L1DT: CacheState, L2T: CacheState
    {
        type EntryRank = (Option<L1IT::EntryRank>, Option<L1DT::EntryRank>, Option<L2T::EntryRank>);
        type EntryIter = itertools::Unique<std::iter::Chain<std::iter::Chain<L1IT::EntryIter, L1DT::EntryIter>, L2T::EntryIter>>;

        fn rank(&self, val: Entry) -> Option<Self::EntryRank> {
            match (self.l1i.rank(val), self.l1d.rank(val), self.l2.rank(val)) {
                (None, None, None) => None,
                (a, b, c) => Some((a, b, c))
            }
        }
        fn entries(&self) -> Self::EntryIter {
            (&self).l1i.entries().chain(self.l1d.entries()).chain(self.l2.entries()).unique()
        }
    }
}
