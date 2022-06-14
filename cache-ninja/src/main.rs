mod state;
mod policy;
mod preset;

use crate::state::{CacheState, Entry};
use crate::policy::{CacheRP, Access, Origin};


fn kickout<T: CacheState, R: CacheRP<State=T>>(t: &T, rp: &R) -> Vec<(Access, T)> {
    //let sres = pathfinding::prelude::bfs(t, |x| rp.successors(x), |x| !x.contains(Entry::T)).unwrap();

    //let (sres, total) = pathfinding::prelude::dijkstra(t, |x| rp.successors_priced(x), |x| !x.contains(Entry::T)).unwrap();
    //println!("[Dijkstra] total cost: {}", total);

    let (sres, total) = pathfinding::prelude::astar(t, |x| rp.successors_priced(x), |x| rp.heur(x, Entry::T), |x| !x.contains(Entry::T)).unwrap();
    println!("[A*] total cost: {}", total);

    //let (sres, total) = pathfinding::prelude::fringe(t, |x| rp.successors_priced(x), |x| rp.heur(x, Entry::T), |x| !x.contains(Entry::T)).unwrap();
    //println!("[Fringe] total cost: {}", total);

    //let (sres, total) = pathfinding::prelude::idastar(t, |x| rp.successors_priced(x), |x| rp.heur(x, Entry::T), |x| !x.contains(Entry::T)).unwrap();
    //println!("[IDA*] total cost: {}", total);

    let mut pst = t;
    let mut accs = Vec::new();
    for st in sres.iter().skip(1) {
        accs.push(rp.find_outedge(pst, st).unwrap());
        pst = st;
    }
    accs.into_iter().zip(sres.into_iter().skip(1)).collect()
}


fn applyseq<T: CacheState, R: CacheRP<State=T>>(t: &T, rp: &R, accs: &Vec<(Entry, Origin)>) -> T {
    let mut st = t.clone();
    println!("Applying sequence to initial state:\n{:?}", st);
    for (ent, orig) in accs.iter().copied() {
        let (ns, acc) = rp.update(&st, ent, orig);
        println!("\n{:?}: {:?}\n{:?}", ent, acc, ns);
        st = ns;
    }
    st
}


fn kickrnd<T: CacheState, R: CacheRP<State=T>>(t: &T, rp: &R, maxrnds: usize, torig: Origin) {
    println!("Kickout max {} rounds; RP: {:?}", maxrnds, rp);
    let mut visited: Vec<T> = Vec::new();
    let mut pst = t.clone();
    let mut ist = rp.update(t, Entry::T, torig).0;
    for ri in 0..maxrnds {
        println!("\n---------------------------");
        if let Some(pos) = visited.iter().position(|x| *x == ist) {
            println!("Round {}; found init state previously in round {}\n{:?}", ri, pos, ist);
            break;
        } else {
            println!("Round {}; init state:\n{:?}", ri, ist);
            println!("Pathfinding...");
            let path = kickout(&ist, rp);
            println!("Found eviction in {} accesses", path.len());
            for (i, (acc, st)) in path.iter().enumerate() {
                println!("\n{}: {:?}\n{:?}", i+1, acc, st);
            }

            println!("\nTrying self-sync\n");
            let pvec = path.iter().map(|(acc, _st)| match acc {
                Access::Hit(_, ent, orig) => (*ent, *orig),
                Access::Miss(ent, orig) => (*ent, *orig)
            }).collect();
            let fst = applyseq(&pst, rp, &pvec);
            if fst == path[path.len()-1].1 {
                println!("\nEnd states MATCH!")
            } else {
                println!("\nEnd states DESYNCED!");
            }

            visited.push(ist);
            pst = path[path.len()-1].1.clone();
            ist = rp.update(&pst, Entry::T, torig).0;
        }
    }
}

fn kickdbl<T: CacheState, R: CacheRP<State=T>>(t: &T, rp: &R, maxrnds: usize) {
    println!("Spliced kickout max {} rounds; RP: {:?}", maxrnds, rp);
    let mut visited: Vec<T> = Vec::new();
    let mut pst = t.clone();
    let mut ist = rp.update_def(t, Entry::T).0;
    let mut nst = rp.update_shallow(t, Entry::T, Default::default()).0;
    for ri in 0..maxrnds {
        println!("\n---------------------------");
        if let Some(pos) = visited.iter().position(|x| *x == ist) {
            println!("Round {}; found init state previously in round {}\n{:?}", ri, pos, ist);
            break;
        } else {
            println!("Round {}; ist:\n{:?}\npst:\n{:?}\nnst:\n{:?}", ri, ist, pst, nst);
            let iist = ist.clone();

            for _si in 0..4 {
                let l1v = rp.evictim1(&pst);
                println!("\nL1 victim is {:?}", l1v);
                if let Entry::P(addr, col) = l1v {
                    println!("Have controlled victim {:?} {:?}", addr, col);

                    // Add relevant pathfinding here
                    let iup = rp.update_def(&ist, l1v);
                    let pup = rp.update_def(&pst, l1v);
                    let nup = rp.update_def(&nst, l1v);
                    println!("Hit ist w/ vict:\n{:?}", iup);
                    println!("Hit pst w/ vict:\n{:?}", pup);
                    println!("Hit nst w/ vict:\n{:?}", nup);
                    ist = iup.0;
                    pst = pup.0;
                    nst = nup.0;

                } else {
                    println!("Uncontrollable victim");
                    break;
                }
            }

            println!("\n=-=-=-=-=-=-=-=\nPhase L2; init state:\n{:?}", ist);
            println!("Pathfinding...");
            let path = kickout(&ist, rp);
            println!("Found eviction in {} accesses", path.len());
            for (i, (acc, st)) in path.iter().enumerate() {
                println!("\n{}: {:?}\n{:?}", i+1, acc, st);
            }

            let pvec = path.iter().map(|(acc, _st)| match acc {
                Access::Hit(_, ent, orig) => (*ent, *orig),
                Access::Miss(ent, orig) => (*ent, *orig)
            }).collect();

            println!("\nTrying self-sync w/ pst\n");
            let fst = applyseq(&pst, rp, &pvec);
            if fst == path[path.len()-1].1 {
                println!("\nEnd states MATCH!")
            } else {
                println!("\nEnd states DESYNCED!");
                println!("{:?}\n{:?}", path[path.len()-1].1, fst);
            }

            println!("\nTrying self-sync w/ l1 noise\n");
            let gst = applyseq(&nst, rp, &pvec);
            if gst == path[path.len()-1].1 {
                println!("\nEnd states MATCH!")
            } else {
                println!("\nEnd states DESYNCED!");
                println!("{:?}\n{:?}", path[path.len()-1].1, fst);
            }

            visited.push(iist);
            pst = path[path.len()-1].1.clone();
            ist = rp.update_def(&pst, Entry::T).0;
            nst = rp.update_shallow(&pst, Entry::T, Default::default()).0;
        }
    }
}


fn main() {
    const MAXROUNDS: usize = 100;

    use crate::preset::*;
    println!("Cache replacement policy simulator");

    //let pres = TLB::IVYBRIDGE;
    //let pres = TLB::HASWELL;
    let pres = TLB::KABYLAKE;
    //let pres = dcache::NEHALEM;
    //let pres = dcache::HASWELL;
    //let pres = dcache::KABYLAKE;

    let rp = pres.rp();
    //let rp = pres.rpx().unwrap();
    let st = pres.newstate();

    kickrnd(&st, &rp, MAXROUNDS, Default::default());
    //kickrnd(&st, &rp, MAXROUNDS, Origin{isnfetch: true});

    //kickdbl(&st, &rp, MAXROUNDS);
}
