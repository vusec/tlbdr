#include <permutation.h>

void __attribute__((optimize("O0"))) walk_dtlb_chain(volatile struct experiment_info *info, volatile pte_t *pte){
    //Warming + getting known state
	for(info->i = 0; info->i < 3 * info->ways; info->i++){
		info->p = read_walk(info->p, &info->iteration);	
	} 

    //Washing the sTLB
    for(info->i = 0; info->i < info->number_of_washings; info->i++){
        info->p = execute_walk(info->p, &info->iteration);
	}

    //Touching the vector index
    info->p = read_walk(info->p, &info->iteration);

    //Desync the TLB
    switch_pages(pte, pte + 1);

    //Visiting 'position' new addresses
    for(info->i = 0; info->i < (info->ways - info->position); info->i++){
	    info->p = read_walk(info->p, &info->iteration);
	}

    //Is it still in?
    info->curr = read(info->p);
}
