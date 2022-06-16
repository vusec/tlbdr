#include <permutation.h>

void __attribute__((optimize("O0"))) walk_stlb_chain(volatile struct experiment_info *info, volatile pte_t *pte){
    //Warming + getting known state
	for(info->i = 0; info->i < 3 * info->ways; info->i++){
		info->p = read_walk(info->p, &info->iteration);
	} 

    //Washing the dTLB
    for(info->i = 0; info->i < info->number_of_washings; info->i++){
		info->p = read_walk(info->p, &info->iteration);
	}

    //Touching the vector index
    info->p = read_walk(info->p, &info->iteration);	

    //Washing the dTLB
    for(info->i = 0; info->i < info->number_of_washings; info->i++){
		info->p = read_walk(info->p, &info->iteration);
	}

    //Desync the TLB
    switch_pages(pte, pte + 1);;

    //Visiting 'position + 1' new addresses
    for(info->i = 0; info->i < (info->ways - info->position); info->i++){
	    info->p = read_walk(info->p, &info->iteration);
    }

    //Is it still in?
    info->curr = read(info->p);
}