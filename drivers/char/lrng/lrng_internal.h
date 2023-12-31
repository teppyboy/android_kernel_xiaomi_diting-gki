/* SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause */
/*
 * Copyright (C) 2018 - 2020, Stephan Mueller <smueller@chronox.de>
 */

#ifndef _LRNG_INTERNAL_H
#define _LRNG_INTERNAL_H

#include <crypto/sha.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

/*************************** General LRNG parameter ***************************/

/* Security strength of LRNG -- this must match DRNG security strength */
#define LRNG_DRNG_SECURITY_STRENGTH_BYTES 32
#define LRNG_DRNG_SECURITY_STRENGTH_BITS (LRNG_DRNG_SECURITY_STRENGTH_BYTES * 8)
#define LRNG_DRNG_BLOCKSIZE 64		/* Maximum of DRNG block sizes */

/*
 * SP800-90A defines a maximum request size of 1<<16 bytes. The given value is
 * considered a safer margin.
 *
 * This value is allowed to be changed.
 */
#define LRNG_DRNG_MAX_REQSIZE		(1<<12)

/*
 * SP800-90A defines a maximum number of requests between reseeds of 2^48.
 * The given value is considered a much safer margin, balancing requests for
 * frequent reseeds with the need to conserve entropy. This value MUST NOT be
 * larger than INT_MAX because it is used in an atomic_t.
 *
 * This value is allowed to be changed.
 */
#define LRNG_DRNG_RESEED_THRESH		(1<<20)

/*
 * Number of interrupts to be recorded to assume that DRNG security strength
 * bits of entropy are received.
 * Note: a value below the DRNG security strength should not be defined as this
 *	 may imply the DRNG can never be fully seeded in case other noise
 *	 sources are unavailable.
 *
 * This value is allowed to be changed.
 */
#define LRNG_IRQ_ENTROPY_BITS		LRNG_DRNG_SECURITY_STRENGTH_BITS

/*
 * Min required seed entropy is 128 bits covering the minimum entropy
 * requirement of SP800-131A and the German BSI's TR02102.
 *
 * This value is allowed to be changed.
 */
#define LRNG_FULL_SEED_ENTROPY_BITS	LRNG_DRNG_SECURITY_STRENGTH_BITS
#define LRNG_MIN_SEED_ENTROPY_BITS	128
#define LRNG_INIT_ENTROPY_BITS		32

/*
 * Wakeup value
 *
 * This value is allowed to be changed but must not be larger than the
 * digest size of the hash operation used update the aux_pool.
 */
#ifdef CONFIG_CRYPTO_LIB_SHA256
# define LRNG_ATOMIC_DIGEST_SIZE	SHA256_DIGEST_SIZE
#else
# define LRNG_ATOMIC_DIGEST_SIZE	SHA1_DIGEST_SIZE
#endif
#define LRNG_WRITE_WAKEUP_ENTROPY	LRNG_ATOMIC_DIGEST_SIZE

/*
 * If the switching support is configured, we must provide support up to
 * the largest digest size. Without switching support, we know it is only
 * the built-in digest size.
 */
#ifdef CONFIG_LRNG_DRNG_SWITCH
# define LRNG_MAX_DIGESTSIZE		64
#else
# define LRNG_MAX_DIGESTSIZE		LRNG_ATOMIC_DIGEST_SIZE
#endif

/*
 * Oversampling factor of IRQ events to obtain
 * LRNG_DRNG_SECURITY_STRENGTH_BYTES. This factor is used when a
 * high-resolution time stamp is not available. In this case, jiffies and
 * register contents are used to fill the entropy pool. These noise sources
 * are much less entropic than the high-resolution timer. The entropy content
 * is the entropy content assumed with LRNG_IRQ_ENTROPY_BITS divided by
 * LRNG_IRQ_OVERSAMPLING_FACTOR.
 *
 * This value is allowed to be changed.
 */
#define LRNG_IRQ_OVERSAMPLING_FACTOR	10

/* Alignmask that is intended to be identical to CRYPTO_MINALIGN */
#define LRNG_KCAPI_ALIGN		ARCH_KMALLOC_MINALIGN

/************************ Default DRNG implementation *************************/

extern struct chacha20_state chacha20;
extern const struct lrng_crypto_cb lrng_cc20_crypto_cb;
void lrng_cc20_init_state(struct chacha20_state *state);
void lrng_cc20_init_state_boot(struct chacha20_state *state);

/********************************** /proc *************************************/

#ifdef CONFIG_SYSCTL
void lrng_pool_inc_numa_node(void);
#else
static inline void lrng_pool_inc_numa_node(void) { }
#endif

/****************************** LRNG interfaces *******************************/

extern u32 lrng_write_wakeup_bits;
extern int lrng_drng_reseed_max_time;

void lrng_writer_wakeup(void);
void lrng_init_wakeup(void);
void lrng_debug_report_seedlevel(const char *name);
void lrng_process_ready_list(void);

/* External interface to use of the switchable DRBG inside the kernel */
void get_random_bytes_full(void *buf, int nbytes);

/************************** Jitter RNG Noise Source ***************************/

#ifdef CONFIG_LRNG_JENT
u32 lrng_get_jent(u8 *outbuf, unsigned int outbuflen);
u32 lrng_jent_entropylevel(void);
#else /* CONFIG_CRYPTO_JITTERENTROPY */
static inline u32 lrng_get_jent(u8 *outbuf, unsigned int outbuflen) {return 0; }
static inline u32 lrng_jent_entropylevel(void) { return 0; }
#endif /* CONFIG_CRYPTO_JITTERENTROPY */

/*************************** CPU-based Noise Source ***************************/

u32 lrng_get_arch(u8 *outbuf);
u32 lrng_slow_noise_req_entropy(u32 required_entropy_bits);

/****************************** DRNG processing *******************************/

/* Secondary DRNG state handle */
struct lrng_drng {
	void *drng;				/* DRNG handle */
	void *hash;				/* Hash handle */
	const struct lrng_crypto_cb *crypto_cb;	/* Crypto callbacks */
	atomic_t requests;			/* Number of DRNG requests */
	unsigned long last_seeded;		/* Last time it was seeded */
	bool fully_seeded;			/* Is DRNG fully seeded? */
	bool force_reseed;			/* Force a reseed */

	/* Lock write operations on DRNG state, DRNG replacement of crypto_cb */
	struct mutex lock;
	spinlock_t spin_lock;
	/* Lock *hash replacement - always take before DRNG lock */
	rwlock_t hash_lock;
};

extern struct mutex lrng_crypto_cb_update;

struct lrng_drng *lrng_drng_init_instance(void);
struct lrng_drng *lrng_drng_atomic_instance(void);

static __always_inline bool lrng_drng_is_atomic(struct lrng_drng *drng)
{
	return (drng->drng == lrng_drng_atomic_instance()->drng);
}

/* Lock the DRNG */
static __always_inline void lrng_drng_lock(struct lrng_drng *drng,
					   unsigned long *flags)
	__acquires(&drng->spin_lock)
{
	/* Use spin lock in case the atomic DRNG context is used */
	if (lrng_drng_is_atomic(drng)) {
		spin_lock_irqsave(&drng->spin_lock, *flags);

		/*
		 * In case a lock transition happened while we were spinning,
		 * catch this case and use the new lock type.
		 */
		if (!lrng_drng_is_atomic(drng)) {
			spin_unlock_irqrestore(&drng->spin_lock, *flags);
			__acquire(&drng->spin_lock);
			mutex_lock(&drng->lock);
		}
	} else {
		__acquire(&drng->spin_lock);
		mutex_lock(&drng->lock);
	}
}

/* Unlock the DRNG */
static __always_inline void lrng_drng_unlock(struct lrng_drng *drng,
					     unsigned long *flags)
	__releases(&drng->spin_lock)
{
	if (lrng_drng_is_atomic(drng)) {
		spin_unlock_irqrestore(&drng->spin_lock, *flags);
	} else {
		mutex_unlock(&drng->lock);
		__release(&drng->spin_lock);
	}
}

void lrng_reset(void);
void lrng_drng_init_early(void);
bool lrng_get_available(void);
void lrng_set_available(void);
void lrng_drng_reset(struct lrng_drng *drng);
int lrng_drng_get_atomic(u8 *outbuf, u32 outbuflen);
int lrng_drng_get_sleep(u8 *outbuf, u32 outbuflen);
void lrng_drng_force_reseed(void);
void lrng_drng_seed_work(struct work_struct *dummy);

#ifdef CONFIG_NUMA
struct lrng_drng **lrng_drng_instances(void);
void lrng_drngs_numa_alloc(void);
#else	/* CONFIG_NUMA */
static inline struct lrng_drng **lrng_drng_instances(void) { return NULL; }
static inline void lrng_drngs_numa_alloc(void) { return; }
#endif /* CONFIG_NUMA */

/************************** Entropy pool management ***************************/

enum lrng_external_noise_source {
	lrng_noise_source_hw,
	lrng_noise_source_user
};

/* Status information about IRQ noise source */
struct lrng_irq_info {
	atomic_t num_events_thresh;	/* Reseed threshold */
	atomic_t reseed_in_progress;	/* Flag for on executing reseed */
	bool irq_highres_timer;	/* Is high-resolution timer available? */
	u32 irq_entropy_bits;	/* LRNG_IRQ_ENTROPY_BITS? */
};

/*
 * This is the entropy pool used by the slow noise source. Its size should
 * be at least as large as LRNG_DRNG_SECURITY_STRENGTH_BITS.
 *
 * The aux pool array is aligned to 8 bytes to comfort the kernel crypto API
 * cipher implementations of the hash functions used to read the pool: for some
 * accelerated implementations, we need an alignment to avoid a realignment
 * which involves memcpy(). The alignment to 8 bytes should satisfy all crypto
 * implementations.
 */
struct lrng_pool {
	/*
	 * Storage for aux data - hash output buffer
	 */
	u8 aux_pool[LRNG_MAX_DIGESTSIZE];
	atomic_t aux_entropy_bits;
	/* All NUMA DRNGs seeded? */
	bool all_online_numa_node_seeded;

	/* Digest size of used hash */
	atomic_t digestsize;
	/* IRQ noise source status info */
	struct lrng_irq_info irq_info;

	/* Serialize read of entropy pool and update of aux pool */
	spinlock_t lock;
};

u32 lrng_entropy_to_data(u32 entropy_bits);
u32 lrng_data_to_entropy(u32 irqnum);
u32 lrng_avail_aux_entropy(void);
void lrng_set_digestsize(u32 digestsize);
u32 lrng_get_digestsize(void);

/* Obtain the security strength of the LRNG in bits */
static inline u32 lrng_security_strength(void)
{
	/*
	 * We use a hash to read the entropy in the entropy pool. According to
	 * SP800-90B table 1, the entropy can be at most the digest size.
	 * Considering this together with the last sentence in section 3.1.5.1.2
	 * the security strength of a (approved) hash is equal to its output
	 * size. On the other hand the entropy cannot be larger than the
	 * security strength of the used DRBG.
	 */
	return min_t(u32, LRNG_FULL_SEED_ENTROPY_BITS,
		     lrng_get_digestsize());
}

void lrng_set_entropy_thresh(u32 new);
void lrng_reset_state(void);

void lrng_pcpu_reset(void);
u32 lrng_pcpu_avail_irqs(void);

static inline u32 lrng_pcpu_avail_entropy(void)
{
	return lrng_data_to_entropy(lrng_pcpu_avail_irqs());
}

static inline u32 lrng_avail_entropy(void)
{
	return lrng_pcpu_avail_entropy() + lrng_avail_aux_entropy();
}

int lrng_pcpu_switch_hash(int node,
			  const struct lrng_crypto_cb *new_cb, void *new_hash,
			  const struct lrng_crypto_cb *old_cb);
u32 lrng_pcpu_pool_hash(struct lrng_pool *pool,
			u8 *outbuf, u32 requested_bits, bool fully_seeded);
void lrng_pcpu_array_add_u32(u32 data);

bool lrng_state_exseed_allow(enum lrng_external_noise_source source);
void lrng_state_exseed_set(enum lrng_external_noise_source source, bool type);
void lrng_state_init_seed_work(void);
bool lrng_state_min_seeded(void);
bool lrng_state_fully_seeded(void);
bool lrng_state_operational(void);

int lrng_pool_trylock(void);
void lrng_pool_unlock(void);
void lrng_pool_all_numa_nodes_seeded(void);
bool lrng_pool_highres_timer(void);
void lrng_pool_set_entropy(u32 entropy_bits);
int lrng_pool_insert_aux(const u8 *inbuf, u32 inbuflen, u32 entropy_bits);
void lrng_pool_add_irq(void);

struct entropy_buf {
	u8 a[LRNG_DRNG_SECURITY_STRENGTH_BYTES];
	u8 b[LRNG_DRNG_SECURITY_STRENGTH_BYTES];
	u8 c[LRNG_DRNG_SECURITY_STRENGTH_BYTES];
	u32 now;
};

int lrng_fill_seed_buffer(struct entropy_buf *entropy_buf);
void lrng_init_ops(u32 seed_bits);

/************************** Health Test linking code **************************/

enum lrng_health_res {
	lrng_health_pass,		/* Health test passes on time stamp */
	lrng_health_fail_use,		/* Time stamp unhealthy, but mix in */
	lrng_health_fail_drop		/* Time stamp unhealthy, drop it */
};

#ifdef CONFIG_LRNG_HEALTH_TESTS
bool lrng_sp80090b_startup_complete(void);
bool lrng_sp80090b_compliant(void);

enum lrng_health_res lrng_health_test(u32 now_time);
void lrng_health_disable(void);

#else	/* CONFIG_LRNG_HEALTH_TESTS */
static inline bool lrng_sp80090b_startup_complete(void) { return true; }
static inline bool lrng_sp80090b_compliant(void) { return false; }

static inline enum lrng_health_res
lrng_health_test(u32 now_time) { return lrng_health_pass; }
static inline void lrng_health_disable(void) { }
#endif	/* CONFIG_LRNG_HEALTH_TESTS */

/****************************** Helper code ***********************************/

static inline u32 atomic_read_u32(atomic_t *v)
{
	return (u32)atomic_read(v);
}

/*************************** Auxiliary functions ******************************/

void invalidate_batched_entropy(void);

/***************************** Testing code ***********************************/

#ifdef CONFIG_LRNG_RAW_HIRES_ENTROPY
bool lrng_raw_hires_entropy_store(u32 value);
#else	/* CONFIG_LRNG_RAW_HIRES_ENTROPY */
static inline bool lrng_raw_hires_entropy_store(u32 value) { return false; }
#endif	/* CONFIG_LRNG_RAW_HIRES_ENTROPY */

#ifdef CONFIG_LRNG_RAW_JIFFIES_ENTROPY
bool lrng_raw_jiffies_entropy_store(u32 value);
#else	/* CONFIG_LRNG_RAW_JIFFIES_ENTROPY */
static inline bool lrng_raw_jiffies_entropy_store(u32 value) { return false; }
#endif	/* CONFIG_LRNG_RAW_JIFFIES_ENTROPY */

#ifdef CONFIG_LRNG_RAW_IRQ_ENTROPY
bool lrng_raw_irq_entropy_store(u32 value);
#else	/* CONFIG_LRNG_RAW_IRQ_ENTROPY */
static inline bool lrng_raw_irq_entropy_store(u32 value) { return false; }
#endif	/* CONFIG_LRNG_RAW_IRQ_ENTROPY */

#ifdef CONFIG_LRNG_RAW_IRQFLAGS_ENTROPY
bool lrng_raw_irqflags_entropy_store(u32 value);
#else	/* CONFIG_LRNG_RAW_IRQFLAGS_ENTROPY */
static inline bool lrng_raw_irqflags_entropy_store(u32 value) { return false; }
#endif	/* CONFIG_LRNG_RAW_IRQFLAGS_ENTROPY */

#ifdef CONFIG_LRNG_RAW_RETIP_ENTROPY
bool lrng_raw_retip_entropy_store(u32 value);
#else	/* CONFIG_LRNG_RAW_RETIP_ENTROPY */
static inline bool lrng_raw_retip_entropy_store(u32 value) { return false; }
#endif	/* CONFIG_LRNG_RAW_RETIP_ENTROPY */

#ifdef CONFIG_LRNG_RAW_REGS_ENTROPY
bool lrng_raw_regs_entropy_store(u32 value);
#else	/* CONFIG_LRNG_RAW_REGS_ENTROPY */
static inline bool lrng_raw_regs_entropy_store(u32 value) { return false; }
#endif	/* CONFIG_LRNG_RAW_REGS_ENTROPY */

#ifdef CONFIG_LRNG_RAW_ARRAY
bool lrng_raw_array_entropy_store(u32 value);
#else	/* CONFIG_LRNG_RAW_ARRAY */
static inline bool lrng_raw_array_entropy_store(u32 value) { return false; }
#endif	/* CONFIG_LRNG_RAW_ARRAY */

#ifdef CONFIG_LRNG_IRQ_PERF
bool lrng_perf_time(u32 start);
#else /* CONFIG_LRNG_IRQ_PERF */
static inline bool lrng_perf_time(u32 start) { return false; }
#endif /*CONFIG_LRNG_IRQ_PERF */

#endif /* _LRNG_INTERNAL_H */
