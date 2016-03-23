// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

// Assembly language pgfault entrypoint defined in lib/pgfaultentry.S.
extern void _pgfault_upcall(void);

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int r;
	pte_t pte;
	int perm = PTE_P|PTE_U|PTE_W;

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at vpt
	//   (see <inc/memlayout.h>).

	// LAB 4: Your code here.
	pte = vpt[VPN(addr)];
	// [!] assert: FEC_WR is a bit flag instead of a single code
	// i.e. assert(there exist a errcode != FEC_WR, but with a FEC_WR)
	if (!( (err & FEC_WR) != 0 && (pte & PTE_COW) != 0 ))
		panic("pgfault handler: user fault at %08x with errcode %08x", addr, err);

	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.
	//   No need to explicitly delete the old page's mapping.
	
	// LAB 4: Your code here.
	if ((r = sys_page_alloc(0, PFTEMP, perm)) < 0) {
		panic("pgfault handler: sys_page_alloc failed");
	}
	memmove(PFTEMP, ROUNDDOWN(addr, PGSIZE), PGSIZE);
	if ((r = sys_page_map(0, PFTEMP, 0, ROUNDDOWN(addr, PGSIZE), perm)) < 0) {
		panic("pgfault handler: sys_page_map failed");
	}
}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why mark ours copy-on-write again
// if it was already copy-on-write?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
// 
static int
duppage(envid_t envid, unsigned pn)
{
	int r;
	void *addr;
	pte_t pte;
	int perm = PTE_P | PTE_U;

	// LAB 4: Your code here.
	addr = (void *)(pn*PGSIZE);
	pte = vpt[pn];
	if ((pte & PTE_W) || (pte & PTE_COW)) {
		perm |= PTE_COW;
	}

	if ((r = sys_page_map(0, addr, envid, addr, perm)) < 0)
		panic("sys_page_map: %e", r);

	if ((pte & PTE_W) || (pte & PTE_COW)) {
		if ((r = sys_page_map(0, addr, 0, addr, perm)) < 0)
			panic("sys_page_map: %e", r);
	}
	return 0;
}

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use vpd, vpt, and duppage.
//   Remember to fix "env" and the user exception stack in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
fork(void)
{
	// LAB 4: Your code here.
	set_pgfault_handler(pgfault);
	envid_t envid = sys_exofork();
	if (envid < 0) {
		// sys_exofork failed
		return envid;
	}
	if (envid == 0) {
		// this is child
		// [?] form the only difference with parent
		envid = sys_getenvid();
		env = &envs[ENVX(envid)];
		// [?] just directly return
		return 0;
	}

	// this is parent
	uintptr_t va;
	int ret;
	int pn;
	for (va = 0; va < UTOP; va += PGSIZE) {
		if (va == UXSTACKTOP-PGSIZE)
			continue;
		if ((vpd[VPD(va)] & PTE_P) == 0)
			continue;
		pn = VPN(va);
		if ((vpt[pn] & PTE_P) && (vpt[pn] & PTE_U)) {
			ret = duppage(envid, pn);
			if (ret < 0) return ret;
		}
	}

	ret = sys_page_alloc(envid, (void *)(UXSTACKTOP-PGSIZE), PTE_P | PTE_U | PTE_W);
	if (ret < 0) panic("fork: page alloc failed");
	ret = sys_env_set_pgfault_upcall(envid, _pgfault_upcall);
	if (ret < 0) panic("fork: register entry point failed");

	ret = sys_env_set_status(envid, ENV_RUNNABLE);
	if (ret < 0) return ret;
	return envid;
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
