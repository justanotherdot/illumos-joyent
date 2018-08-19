
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright (c) 2013 Joyent, Inc.  All rights reserved.
 */

/*
 * Fake stubs that we need to advance
 */
extern void bop_panic(const char *);

#define	STUB(x) void x(void) { bop_panic(#x); }

STUB(reset)
STUB(prom_enter_mon)

STUB(dtrace_safe_synchronous_signal)
STUB(sendsig32)
STUB(prstop)
STUB(prnotify)
STUB(prnostep)
STUB(sendsig)
STUB(audit_core_start)
STUB(dtrace_safe_defer_signal)
STUB(audit_core_finish)


STUB(atomic_add_64)

STUB(spl0)
STUB(cu_pg_update)
STUB(siron)
STUB(panic_saveregs)
STUB(panic_savetrap)
STUB(unset_idle_cpu)
STUB(mp_cpu_poweron)
STUB(turnstile_lookup)
STUB(turnstile_stay_asleep)
STUB(mapexec32_brand)
STUB(ddi_modclose)
STUB(panic_quiesce_hw)
STUB(panic_stopcpus)
STUB(turnstile_wakeup)
STUB(mp_cpu_poweroff)
STUB(turnstile_block)
STUB(ddi_modopen)
STUB(cpu_create_intrstat)
STUB(mp_cpu_faulted_enter)
STUB(cmp_set_nosteal_interval)
STUB(pg_plat_hw_shared)
STUB(cpupm_plat_domain_id)
STUB(bp_color)
STUB(pg_plat_cmt_policy)
STUB(siron_poke_cpu)
STUB(mod_remove)
STUB(getpil)
STUB(panic_showtrap)
STUB(cpu_disable_intr)
STUB(resume)
STUB(setjmp)
STUB(traceregs)
STUB(unscalehrtime)
STUB(cpupm_plat_state_enumerate)
STUB(mp_cpu_stop)
STUB(membar_sync)
STUB(membar_exit)
STUB(cpu_intr_swtch_exit)
STUB(pg_plat_cpus_share)
STUB(lgrp_root)
STUB(elf32exec)
STUB(pg_plat_hw_rank)
STUB(resume_from_zombie)
STUB(cpu_enable_intr)
STUB(resume_from_intr)
STUB(mp_cpu_faulted_exit)
STUB(turnstile_change_pri)
STUB(mp_cpu_unconfigure)
STUB(pg_plat_get_core_id)
STUB(get_cpu_mstate)
STUB(elfexec)
STUB(pg_plat_hw_instance_id)
STUB(atomic_cas_ulong)
STUB(atomic_clear_long_excl)
STUB(mapexec_brand)
STUB(panic_trigger)
STUB(cpu_delete_intrstat)
STUB(panic_dump_hw)
STUB(cpu_intr_swtch_enter)
STUB(panic_enter_hw)
STUB(cpupm_plat_change_state)
STUB(mp_cpu_start)
STUB(turnstile_exit)
STUB(fss_changepset)
STUB(mp_cpu_configure)
STUB(set_idle_cpu)
STUB(mach_cpu_pause)
STUB(kdi_siron)
STUB(ld_ib_prop)

STUB(anon_init)
STUB(cu_init)
STUB(vfsinit)

STUB(cpr)
STUB(acct)
STUB(bind)
STUB(recv)
STUB(send)
STUB(spl7)
STUB(spl8)
STUB(splx)
STUB(page_get_user_pagesize)
STUB(audit_init_module)
STUB(page_pptonum)
STUB(i_ddi_intr_ops)
STUB(dcopy_cmd_alloc)
STUB(impl_acc_hdl_free)
STUB(plat_hold_page)
STUB(check_status)
STUB(audit_symlink_create)
STUB(page_pp_lock)
STUB(lwpchan_delete_mapping)
STUB(lwp_mutex_register)
STUB(atomic_dec_ulong_nv)
STUB(hat_page_clrattr)
STUB(copyinstr_noerr)
STUB(modunload_disable)
STUB(audit)
STUB(copyoutstr)
STUB(getfp)
STUB(indir)
STUB(idmap_reg_dh)
STUB(kcopy)
STUB(kzero)
STUB(nosys)
STUB(fifo_vfastoff)
STUB(splhi)
STUB(ucopy)
STUB(uzero)
STUB(lwp_sema_post)
STUB(page_subclaim_pages)
STUB(lwp_mutex_wakeup)
STUB(i_ddi_acc_clr_fault)
STUB(audit_setf)
STUB(audit_priv)
STUB(audit_exec)
STUB(audit_exit)
STUB(hat_leave_region)
STUB(door_ki_upcall)
STUB(thread_onproc)
STUB(impl_make_parlist)
STUB(lwp_cond_signal)
STUB(set_proc_ast)
STUB(ddi_rep_put64)
STUB(ddi_rep_put32)
STUB(ddi_rep_put16)
STUB(group_page_unlock)
STUB(boot_virt_alloc)
STUB(ddi_rep_get64)
STUB(ddi_rep_get32)
STUB(ddi_rep_get16)
STUB(i_ddi_map_fault)
STUB(lwp_stk_cache_init)
STUB(vfs_list_read_lock)
STUB(dtrace_interrupt_enable)
STUB(ftrace_interrupt_enable)
STUB(kcpc_passivate)
STUB(lwp_mutex_unlock)
STUB(anon_private)
STUB(anon_swap_adjust)
STUB(xcopyout_nta)
STUB(segkmem_alloc_lp)
STUB(hat_pagesync)
STUB(console_enter)
STUB(spec_snode_walk)
STUB(read_binding_file)
STUB(audit_chdirec)
STUB(prinvalidate)
STUB(mod_name_to_major)
STUB(mod_name_to_modid)
STUB(vfs_mntpoint2vfsp)
STUB(lock_clear)
STUB(membar_enter)
STUB(ka_init)
STUB(page_pp_unlock)
STUB(page_io_unlock)
STUB(vfs_getvfsswbyvfsops)
STUB(loadable_syscall)
STUB(unlock_hw_class_list)
STUB(sockconfig)
STUB(fuword8_noerr)
STUB(lwp_detach_brand_hdlrs)
STUB(mod_hash_null_keydtor)
STUB(valid_va_range_aligned)
STUB(lwp_forkregs)
STUB(devfs_devpolicy)
STUB(hat_stats_disable)
STUB(vfs_freevfsops_by_type)
STUB(page_tryupgrade)
STUB(mod_hash_create_ptrhash)
STUB(page_num_user_pagesizes)
STUB(pr_free_watched_pages)
STUB(install_utrap)
STUB(fsop_mountroot)
STUB(page_vnode_mutex)
STUB(dtrace_membar_consumer)
STUB(atomic_or_ulong)
STUB(socket_sendmblk)
STUB(audit_symlink)
STUB(i_ddi_apply_range)
STUB(lock_clear_splx)
STUB(page_get_pagesize)
STUB(audit_strputmsg)
STUB(thread_transition)
STUB(i_ddi_alloc_intr_phdl)
STUB(i_ddi_acc_set_fault)
STUB(mod_rele_dev_by_major)
STUB(mod_hold_dev_by_major)
STUB(clconf_get_nodeid)
STUB(e_ddi_copyfromdev)
STUB(atomic_inc_uint)
STUB(impl_acc_hdl_alloc)
STUB(mod_hash_destroy_ptrhash)
STUB(atomic_cas_ptr)
STUB(sdev_devstate_change)
STUB(translate_devid)
STUB(lwpchan_destroy_cache)
STUB(impl_keep_instance)
STUB(impl_free_parlist)
STUB(hat_stats_enable)
STUB(hr_clock_unlock)
STUB(audit_closef)
STUB(hat_join_srd)
STUB(hat_softlock)
STUB(spec_is_clone)
STUB(audit_fdsend)
STUB(audit_fdrecv)
STUB(random_get_bytes)
STUB(audit_finish)
STUB(page_destroy_free)
STUB(fs_build_vector)
STUB(pf_is_memory)
STUB(peekpoke_mem)
STUB(ddi_fm_capable)
STUB(page_try_demote_pages)
STUB(fastboot_update_config)
STUB(audit_savepath)
STUB(hat_get_mapped_size)
STUB(impl_parlist_to_major)
STUB(thread_lock_high)
STUB(lock_hw_class_list)
STUB(segkmem_free_lp)
STUB(mod_hash_insert_reserve)
STUB(thread_stk_init)
STUB(hat_free_start)
STUB(impl_ddi_sunbus_initchild)
STUB(lwp_rtt)
STUB(prlwpfree)
STUB(prlwpexit)
STUB(page_lookup_create)
STUB(hat_memload)
STUB(page_io_locked)
STUB(console_exit)
STUB(page_lookup_nowait)
STUB(map_addr_vacalign_check)
STUB(hat_pageunload)
STUB(spec_fence_snode)
STUB(page_alloc_pages)
STUB(copyout_noerr)
STUB(audit_vncreate_finish)
STUB(anon_alloc)
STUB(modload)
STUB(modreap)
STUB(on_fault)
STUB(door_ki_lookup)
STUB(anon_pages)
STUB(lbolt_softint_post)
STUB(page_migrate)
STUB(door_revoke_all)
STUB(ndi_fmc_entry_error)
STUB(anon_array_try_enter)
STUB(spec_is_selfclone)
STUB(page_destroy_pages)
STUB(anon_get_slot)
STUB(set_anoninfo)
STUB(prefetch_write_many)
STUB(dump_plat_addr)
STUB(dump_plat_data)
STUB(au_to_arg32)
STUB(mod_hash_iddata_gen)
STUB(anon_shmap_free_pages)
STUB(random_get_pseudo_bytes)
STUB(num_phys_pages)
STUB(page_relocate_cage)
STUB(mutex_tryenter)
STUB(no_fault)
STUB(k_anoninfo)
STUB(anon_dup)
STUB(sync_icache)
STUB(vfs_rlock_wait)
STUB(lock_try)
STUB(lock_set)
STUB(sock_getmsg)
STUB(getsetcontext)
STUB(page_mark_migrate)
STUB(i_ddi_rnumber_to_regspec)
STUB(page_exists)
STUB(lock_spin_try)
STUB(anon_create)
STUB(anon_decref)
STUB(anon_fill_cow_holes)
STUB(vfs_getops)
STUB(vfs_getresource)
STUB(mod_getctl)
STUB(hwc_free_spec_list)
STUB(vfs_has_feature)
STUB(lwp_cond_broadcast)
STUB(_insque)
STUB(sock_putmsg)
STUB(pr_isself)
STUB(save_syscall_args)
STUB(getsockname)
STUB(fss_allocbuf)
STUB(poke_cpu)
STUB(page_io_trylock)
STUB(anon_grow)
STUB(non_anon)
STUB(anon_free)
STUB(lbolt_softint_add)
STUB(on_trap)
STUB(anon_zero)
STUB(vfs_unrefvfssw)
STUB(anon_dup_fill_holes)
STUB(page_lookup)
STUB(ip_ocsum)
STUB(fsop_vget)
STUB(fsop_root)
STUB(audit_vncreate_start)
STUB(i_ddi_free_intr_phdl)
STUB(accept)
STUB(atomic_set_long_excl)
STUB(kcopy_nta)
STUB(audit_devpolicy)
STUB(door_exit)
STUB(door_fork)
STUB(door_slam)
STUB(_remque)
STUB(valid_usr_range)
STUB(i_ddi_bus_map)
STUB(caller)
STUB(snf_segmap)
STUB(so_socket)
STUB(copyin)
STUB(socket_setsockopt)
STUB(getpcstack)
STUB(va_to_pfn)
STUB(mod_sysctl)
STUB(mod_sysvar)
STUB(specfind)
STUB(i_ddi_drv_ereport_post)
STUB(gethrestime_sec)
STUB(suword64_noerr)
STUB(hat_unlock)
STUB(fuword64_noerr)
STUB(vfs_unlock)
STUB(fsop_statfs)
STUB(ovbcopy)
STUB(au_uwrite)
STUB(page_unlock)
STUB(page_unresv)
STUB(disp_lock_enter_high)
STUB(spec_segmap)
STUB(get_udatamodel)
STUB(vfs_lock_wait)
STUB(exec_set_sp)
STUB(ddi_fm_ereport_post)
STUB(copyin_noerr)
STUB(pty_initspace)
STUB(audit_setppriv)
STUB(page_szc_lock)
STUB(listen)
STUB(lowbit)
STUB(mdboot)
STUB(modctl)
STUB(door_ki_rele)
STUB(door_ki_hold)
STUB(door_ki_info)
STUB(door_ki_open)
STUB(i_ddi_add_softint)
STUB(prexit)
STUB(prfree)
STUB(prstep)
STUB(lwp_cond_wait)
STUB(recv32)
STUB(kpreempt)
STUB(mdpreboot)
STUB(hr_clock_lock)
STUB(prrelvm)
STUB(send32)
STUB(sendto)
STUB(subyte)
STUB(idmap_get_door)
STUB(vfs_devismounted)
STUB(membar_consumer)
STUB(vpanic)
STUB(page_add)
STUB(page_sub)
STUB(page_szc)
STUB(pagezero)
STUB(i_ddi_remove_softint)
STUB(dcopy_free)
STUB(mod_read_system_file)
STUB(thread_stop)
STUB(vfs_setmntopt)
STUB(copyinstr)
STUB(thread_lock)
STUB(thread_load)
STUB(makectty)
STUB(set_all_zone_usr_proc_sys)
STUB(hat_flush_range)
STUB(impl_assign_instance)
STUB(randtick)
STUB(copyoutstr_noerr)
STUB(hat_memload_region)
STUB(map_addr)
STUB(map_pgsz)
STUB(kphysm_setup_func_register)
STUB(kcage_cageout_wakeup)
STUB(devi_stillreferenced)
STUB(i_ddi_cacheattr_to_hatacc)
STUB(spec_unfence_snode)
STUB(i_ddi_devacc_to_hatacc)
STUB(prbarrier)
STUB(audit_setfsat_path)
STUB(hat_dump)
STUB(hat_exit)
STUB(hat_sync)
STUB(gethrestime)
STUB(suword8_noerr)
STUB(recvmsg)
STUB(suword16_noerr)
STUB(fuword16_noerr)
STUB(anon_disclaim)
STUB(au_free_rec)
STUB(clconf_maximum_nodeid)
STUB(ndi_fm_handler_dispatch)
STUB(hwc_get_child_spec)
STUB(page_create_putback)
STUB(i_mod_hash_insert_nosync)
STUB(mod_hash_destroy_hash)
STUB(page_io_wait)
STUB(page_io_lock)
STUB(devfs_clean)
STUB(ddi_periodic_fini)
STUB(sysdc_thread_enter)
STUB(dump_plat_pfn)
STUB(anon_get_next_ptr)
STUB(fs_error)
STUB(ddi_fm_acc_err_get)
STUB(kcage_on)
STUB(hat_chgprot)
STUB(hat_chgattr)
STUB(syscall_ap)
STUB(page_exists_physcontig)
STUB(tnf_opaque_array_1)
STUB(map_pgszcvec)
STUB(lwp_setrval)
STUB(semexit)
STUB(sendmsg)
STUB(page_szc_user_filtered)
STUB(setregs)
STUB(free_vp_pages)
STUB(page_hashout)
STUB(modunload_enable)
STUB(shmexit)
STUB(shmfork)
STUB(i_ddi_mem_alloc)
STUB(hat_supported)
STUB(mod_hash_clear)
STUB(mod_hash_bystr)
STUB(spec_assoc_vp_with_devi)
STUB(dcopy_cmd_post)
STUB(dcopy_cmd_poll)
STUB(dcopy_cmd_free)
STUB(i_ddi_intr_redist_all_cpus)
STUB(impl_fix_props)
STUB(dld_autopush)
STUB(cladmin)
STUB(pr_isobject)
STUB(spec_devi_open_count)
STUB(lwp_rtt_initial)
STUB(hat_clrattr)
STUB(hat_alloc)
STUB(get_class)
STUB(hat_enter)
STUB(set_errno)
STUB(setsockopt)
STUB(getsockopt)
STUB(mod_major_to_name)
STUB(connect)
STUB(hat_probe)
STUB(copyout)
STUB(copystr)
STUB(ucopystr)
STUB(hat_share)
STUB(hat_setup)
STUB(splhigh)
STUB(hat_kpm_vaddr2page)
STUB(lwp_mutex_timedlock)
STUB(vfs_syncall)
STUB(mod_hash_create_idhash)
STUB(hat_page_getshare)
STUB(i_untimeout)
STUB(disp_lock_exit)
STUB(hat_unlock_region)
STUB(hat_swapout)
STUB(sulword)
STUB(systeminfo)
STUB(fastboot_update_and_load)
STUB(suword8)
STUB(anon_map_getpages)
STUB(ddi_get8)
STUB(ddi_put8)
STUB(anon_map_privatepages)
STUB(gethrtime)
STUB(i_timeout)
STUB(fifo_getinfo)
STUB(anon_array_enter)
STUB(auditdoor)
STUB(ddi_rep_put8)
STUB(ddi_rep_get8)
STUB(group_page_trylock)
STUB(hat_page_checkshare)
STUB(read_dacf_binding_file)
STUB(vfs_unmountall)
STUB(ddi_fm_acc_err_clear)
STUB(disp_lock_enter)
STUB(impl_ddi_prop_int_from_prom)
STUB(anon_get_ptr)
STUB(anon_getpage)
STUB(vfs_dev2vfsp)
STUB(tod_get)
STUB(tod_set)
STUB(page_first)
STUB(vfs_setfsops)
STUB(au_doormsg)
STUB(free_pages)
STUB(page_nextn)
STUB(domount)
STUB(make_mbind)
STUB(page_vpadd)
STUB(page_vpsub)
STUB(disp_lock_exit_nopreempt)
STUB(vfs_getvfssw)
STUB(page_free_replacement_page)
STUB(nl7c_sendfilev)
STUB(scalehrtime)
STUB(fsop_vnstate)
STUB(page_downgrade)
STUB(atomic_and_ulong)
STUB(so_socketpair)
STUB(getpeername)
STUB(hat_page_getattr)
STUB(recvfrom)
STUB(i_ddi_check_cache_attr)
STUB(hat_memload_array)
STUB(atomic_swap_uint)
STUB(getuserpc)
STUB(atomic_add_64_nv)
STUB(atomic_add_32_nv)
STUB(prexecstart)
STUB(anon_map_createpages)
STUB(anon_map_demotepages)
STUB(mod_hash_create_extended)
STUB(i_mod_hash_clear_nosync)
STUB(lwp_rwlock_sys)
STUB(hat_unload_callback)
STUB(vfs_refvfssw)
STUB(door_ki_upcall_limited)
STUB(hat_kpm_page2va)
STUB(gethrtime_waitfree)
STUB(hat_unshare)
STUB(sendto32)
STUB(page_create_va)
STUB(mod_hash_strkey_cmp)
STUB(dounmount)
STUB(mod_hash_null_valdtor)
STUB(page_numtopp_nolock)
STUB(atomic_inc_ulong)
STUB(atomic_dec_ulong)
STUB(i_ddi_set_softint_pri)
STUB(makespecvp)
STUB(page_promote_size)
STUB(atomic_add_long)
STUB(common_specvp)
STUB(suword32_noerr)
STUB(fuword32_noerr)
STUB(page_iolock_assert)
STUB(page_addclaim)
STUB(atomic_inc_64_nv)
STUB(mod_hash_remove)
STUB(plat_tod_fault)
STUB(atomic_inc_32_nv)
STUB(atomic_dec_32_nv)
STUB(recvfrom32)
STUB(mutex_sync)
STUB(suword64)
STUB(suword32)
STUB(suword16)
STUB(fuword64)
STUB(fuword16)
STUB(fuword32)
STUB(hat_join_region)
STUB(kidmap_getsidbygid)
STUB(prexecend)
STUB(kidmap_getgidbysid)
STUB(kidmap_getuidbysid)
STUB(kidmap_getsidbyuid)
STUB(vfs_getmntpoint)
STUB(page_trylock)
STUB(mod_hash_create_strhash)
STUB(impl_acc_hdl_get)
STUB(atomic_dec_64)
STUB(atomic_dec_32)
STUB(lwp_sema_trywait)
STUB(anon_free_pages)
STUB(mod_hash_reserve)
STUB(page_get_pagecnt)
STUB(mod_hash_replace)
STUB(atomic_cas_64)
STUB(atomic_cas_32)
STUB(i_ddi_trigger_softint)
STUB(exec_get_spslew)
STUB(debug_enter)
STUB(pr_allstopped)
STUB(vfs_optionisset)
STUB(anon_swap_free)
STUB(zfs_prop_to_name)
STUB(dtrace_membar_producer)
STUB(idmap_purge_cache)
STUB(atomic_inc_64)
STUB(atomic_inc_32)
STUB(segkmem_lpsetup)
STUB(page_list_next)
STUB(ndi_fm_init)
STUB(dtrace_gethrtime)
STUB(vfs_makefsops)
STUB(sosendfile64)
STUB(prefetch_smap_w)
STUB(hat_getpagesize)
STUB(devfs_walk)
STUB(vfs_hold)
STUB(vfs_rele)
STUB(vfs_sync)
STUB(mod_hash_walk)
STUB(mod_hash_find)
STUB(mod_hash_byid)
STUB(hat_getattr)
STUB(prefetch_page_r)
STUB(page_destroy)
STUB(anon_unresvmem)
STUB(kcage_tick)
STUB(page_free)
STUB(page_find)
STUB(page_next)
STUB(page_lock)
STUB(page_resv)
STUB(fulword)
STUB(atomic_dec_uint)
STUB(i_mod_hash_find_nosync)
STUB(atomic_add_32)
STUB(atomic_add_16)
STUB(fuword8)
STUB(hat_kpm_fault)
STUB(fss_freebuf)
STUB(hat_memload_array_region)
STUB(hat_kpm_mapin)
STUB(set_freemem)
STUB(page_subclaim)
STUB(spec_getvnodeops)
STUB(anonmap_alloc)
STUB(hat_thread_exit)
STUB(anon_swap_restore)
STUB(delete_mbind)
STUB(mach_sysconfig)
STUB(anon_copy_ptr)
STUB(kcage_cageout_init)
STUB(hat_dup_region)
STUB(getsetcontext32)
STUB(anonmap_purge)
STUB(mutex_owner)
STUB(anon_release)
STUB(fss_changeproj)
STUB(lwp_stk_init)
STUB(lwp_stk_fini)
STUB(disp_lock_exit_high)
STUB(anon_resvmem)
STUB(hat_free_end)
STUB(lwp_pcb_exit)
STUB(lwp_load)
STUB(page_relocate)
STUB(orphanlist)
STUB(hat_dup)
STUB(hat_map)
STUB(mod_nodev_ops)
STUB(atomic_add_long_nv)
STUB(hat_kpm_mapout)
STUB(set_proc_post_sys)
STUB(e_ddi_copytodev)
STUB(anon_array_exit)
STUB(lwp_sema_timedwait)
STUB(idmap_unreg_dh)
STUB(modgetsymname)
STUB(gethrestime_lasttick)
STUB(atomic_cas_uint)
STUB(mod_containing_pc)
STUB(anonmap_free)
STUB(upimutex_cleanup)
STUB(driver_active)
STUB(lock_set_spl)
STUB(highbit)
STUB(cl_flk_state_transition_notify)
STUB(drv_usecwait)
STUB(page_list_concat)
STUB(set_base_spl)
STUB(ftrace_interrupt_disable)
STUB(mod_hash_destroy)
STUB(impl_free_instance)
STUB(lwp_mutex_trylock)
STUB(page_num_pagesizes)
STUB(fsop_sync_by_kind)
STUB(intr_passivate)
STUB(dcopy_alloc)
STUB(valid_va_range)
STUB(anon_set_ptr)
STUB(ddi_get64)
STUB(ddi_get32)
STUB(ddi_get16)
STUB(ddi_put64)
STUB(ddi_put32)
STUB(ddi_put16)
STUB(sock_getfasync)
STUB(dtrace_interrupt_disable)
STUB(lwp_freeregs)
STUB(xcopyin_nta)
STUB(i_ddi_mem_free)
STUB(hat_page_setattr)
STUB(page_get_shift)
STUB(page_addclaim_pages)
STUB(ndi_fmc_entry_error_all)
STUB(impl_setup_ddi)
STUB(lwp_getdatamodel)
STUB(mod_hash_insert)
STUB(shutdown)
STUB(mod_hash_cancel)
STUB(audit_anchorpath)
STUB(i_convert_boot_device_name)
STUB(mod_hash_destroy_strhash)
STUB(dsl_prop_get)
STUB(page_release)
STUB(vfs_list_unlock)
STUB(vfs_syncprogress)

STUB(halt)
STUB(segkmem_gc)
STUB(cbe_init_pre)
STUB(reset_syscall_args)
STUB(mod_uninstall_daemon)
STUB(start_other_cpus)
STUB(vfs_mountroot)
STUB(cbe_init)
STUB(ddi_periodic_init)
STUB(consconfig)
STUB(cluster)
STUB(startup)
STUB(strplumb)
STUB(vm_init)
STUB(post_startup)
STUB(release_bootstrap)

void
bcmp(void)
{
	bop_panic("bcmp");
}

void
kdi_flush_caches(void)
{
	bop_panic("kdi_flush_caches");
}

void
kobj_text_alloc(void)
{
	bop_panic("kobj_text_alloc");
}

void
mod_release_mod(void)
{
	bop_panic("mod_release_mod");
}

void
kdi_range_is_nontoxic(void)
{
	bop_panic("kdi_range_is_nontoxic");
}

void
dcache_flushall(void)
{
	bop_panic("dcache_flushall");
}

void
SHA1Final(void)
{
	bop_panic("SHA1Final");
}
void
moddebug(void)
{
	bop_panic("moddebug");
}
void
kdi_pwrite(void)
{
	bop_panic("kdi_pwrite");
}

void
e_data(void)
{
	bop_panic("e_data");
}

void
e_text(void)
{
	bop_panic("e_text");
}

void
SHA1Update(void)
{
	bop_panic("SHA1Update");
}

void
hat_unload(void)
{
	bop_panic("hat_unload");
}
void
s_data(void)
{
	bop_panic("s_data");
}

void
s_text(void)
{
	bop_panic("s_text");
}


void
stubs_base(void)
{
	bop_panic("stubs_base");
}

void
rw_exit(void)
{
	bop_panic("rw_exit");
}

void
hat_getpfnum(void)
{
	bop_panic("hat_getpfnum");
}

void
mod_load_requisite(void)
{
	bop_panic("mod_load_requisite");
}

void
hat_devload(void)
{
	bop_panic("hat_devload");
}


void
kdi_vtop(void)
{
	bop_panic("kdi_vtop");
}

void
stubs_end(void)
{
	bop_panic("stubs_end");
}
void
SHA1Init(void)
{
	bop_panic("SHA1Init");
}

void
membar_producer(void)
{
	bop_panic("membar_producer");
}

void
mutex_exit(void)
{
	bop_panic("mutex_exit");
}

void
mutex_init(void)
{
	bop_panic("mutex_init");
}

void
segkmem_alloc(void)
{
	bop_panic("segkmem_alloc");
}

void
rw_enter(void)
{
	bop_panic("rw_enter");
}

void
segkmem_free(void)
{
	bop_panic("segkmem_free");
}

void
mutex_destroy(void)
{
	bop_panic("mutex_destroy");
}

void
mutex_enter(void)
{
	bop_panic("mutex_enter");
}

void
kobj_texthole_free(void)
{
	bop_panic("kobj_texthole_free");
}

void
kdi_pread(void)
{
	bop_panic("kdi_pread");
}

void
kobj_vmem_init(void)
{
	bop_panic("kobj_vmem_init");
}

void
mutex_owned(void)
{
	bop_panic("mutex_owned");
}
