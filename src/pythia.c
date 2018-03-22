/**
 * Copyright (C) 2015-2018 Virgil Security Inc.
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     (1) Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *     (2) Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *
 *     (3) Neither the name of the copyright holder nor the names of its
 *     contributors may be used to endorse or promote products derived from
 *     this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ''AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Lead Maintainer: Virgil Security Inc. <support@virgilsecurity.com>
 */

#include "pythia.h"
#include "pythia_init.h"
#include "pythia_conf.h"

static bn_t g1_ord;
static g1_t g1_gen;
static bn_t gt_ord;
static gt_t gt_gen;

int pythia_init() {
    if (core_get())
        return 0;

    pythia_err_init();

    if (core_init() != STS_OK)
        return -1;

    if (ep_param_set_any_pairf() != STS_OK)
        return -1;

    bn_null(g1_ord);
    g1_null(g1_gen);
    bn_null(gt_ord);
    gt_null(gt_gen);

    TRY {
        bn_new(g1_ord);
        g1_get_ord(g1_ord);

        g1_new(g1_gen);
        g1_get_gen(g1_gen);

        bn_new(gt_ord);
        gt_get_ord(gt_ord);

        gt_new(gt_gen);
        gt_get_gen(gt_gen);
    }
    CATCH_ANY {
        gt_free(gt_gen);
        bn_free(gt_ord);
        g1_free(g1_gen);
        bn_free(g1_ord);

        return -1;
    }

    return 0;
}

int pythia_deinit() {
    return core_clean();
}

void pythia_err_init() {
    err_core_reset_default();
}

void randomZ(bn_t r, bn_t max) {
    if (!max) {
        bn_rand(r, BN_POS, 384);
    }
    else {
        bn_rand_mod(r, max);
    }
}

void hashG1(ep_t g1, const uint8_t *msg, int msg_size) {
    uint8_t hash[MD_LEN_SH384];
    md_map_sh384(hash, msg, msg_size);

    g1_map(g1, hash, MD_LEN_SH384);
}

void hashG2(ep2_t g2, const uint8_t *msg, int msg_size) {
    uint8_t hash[MD_LEN_SH384];
    md_map_sh384(hash, msg, msg_size);

    g2_map(g2, hash, MD_LEN_SH384);
}

void pythia_blind(ep_t blinded, bn_t rInv, const uint8_t *msg, int msg_size) {
    bn_t r;
    bn_null(r);

    bn_t gcd, bn_one;
    bn_null(gcd); bn_null(bn_one);

    ep_t g1;
    ep_null(g1);

    TRY {
        bn_new(r);
        bn_new(bn_one);
        bn_set_bit(bn_one, 1, 1);
        bn_new(gcd);
        do {
            randomZ(r, NULL);
            bn_gcd_ext(gcd, rInv, NULL, r, g1_ord);
        } while (!bn_cmp(gcd, bn_one));

        ep_new(g1);
        hashG1(g1, msg, msg_size);

        ep_mul(blinded, g1, r);
    }
    CATCH_ANY {
        THROW(ERR_CAUGHT);
    }
    FINALLY {
        ep_free(g1);

        bn_free(gcd);
        bn_free(bn_one);
        bn_free(r);
    }
}

void genKw(bn_t kw, const uint8_t *w, int w_size, const uint8_t *msk, int msk_size, const uint8_t *z, int z_size) {
    uint8_t mac[MD_LEN_SH384];
    uint8_t *zw = NULL;

    bn_t b; bn_null(b);

    TRY {
        zw = calloc((size_t) (z_size + w_size), sizeof(uint8_t));
        memcpy(zw, z, z_size);
        memcpy(zw + z_size, w, w_size);

        md_hmac(mac, zw, z_size + w_size, msk, msk_size);

        bn_new(b);
        bn_read_bin(b, mac, MD_LEN_SH384);

        bn_mod(kw, b, gt_ord);
    }
    CATCH_ANY {
        THROW(ERR_CAUGHT);
    }
    FINALLY {
        bn_free(b);
        free(zw);
    }
}

void pythia_eval(gt_t y, bn_t kw, ep2_t tTilde,
          const uint8_t *w, int w_size, const uint8_t *t, int t_size, ep_t x,
          const uint8_t *msk, int msk_size, const uint8_t *s, int s_size) {
    ep_t xKw;
    ep_null(xKw);

    TRY {
        genKw(kw, w, w_size, msk, msk_size, s, s_size);

        hashG2(tTilde, t, t_size);

        ep_new(xKw);
        ep_mul(xKw, x, kw);

        pc_map(y, xKw, tTilde);
    }
    CATCH_ANY {
        THROW(ERR_CAUGHT);
    }
    FINALLY {
        ep_free(xKw);
    }
}

void gt_pow(gt_t res, gt_t a, bn_t exp) {
    bn_t e; bn_null(e);

    TRY {
        bn_new(e);
        bn_mod(e, exp, gt_ord);

        gt_exp(res, a, e);
    }
    CATCH_ANY {
        THROW(ERR_CAUGHT);
    }
    FINALLY {
        bn_free(e);
    }
}

void pythia_deblind(gt_t a, gt_t y, bn_t rInv) {
    TRY {
        gt_pow(a, y, rInv);
    }
    CATCH_ANY {
        THROW(ERR_CAUGHT);
    }
    FINALLY {}
}

void hashZ(bn_t hash, const uint8_t* const * args, int args_size, const int* args_sizes) {
    const uint8_t tag_msg[31] = "TAG_RELIC_HASH_ZMESSAGE_HASH_Z";
    uint8_t *c = NULL;

    TRY {
        int total_size = 0;
        for (int i = 0; i < args_size; i++)
            total_size += args_sizes[i];

        c = calloc((size_t)total_size, sizeof(uint8_t));

        uint8_t *p = c;
        for (int i = 0; i < args_size; i++) {
            memcpy(p, args[i], args_sizes[i]);
            p += args_sizes[i];
        }

        uint8_t mac[MD_LEN];
        md_hmac(mac, c, total_size, tag_msg, 31);

        bn_read_bin(hash, mac, MD_LEN);
    }
    CATCH_ANY {
            THROW(ERR_CAUGHT);
    }
    FINALLY {
        free(c);
    }
}

void scalar_mul_g1(g1_t r, const g1_t p, /*IN*/ bn_t a, /*IN*/ bn_t n) {
    bn_t mod; bn_null(mod);

    TRY {
        bn_new(mod);
        bn_mod(mod, a, n);

        g1_mul(r, p, mod);
    }
    CATCH_ANY {
        THROW(ERR_CAUGHT);
    }
    FINALLY {
        bn_free(mod);
    }
}

void serialize_g1(uint8_t *r, int size, const g1_t x) {
    g1_write_bin(r, size, x, 1);
}

void serialize_gt(uint8_t *r, int size, gt_t x) {
    gt_write_bin(r, size, x, 1);
}

void pythia_prove(g1_t p, bn_t c, bn_t u, const g1_t x, const g2_t tTilde, /*IN*/ bn_t kw, /*IN*/ gt_t y) {
    gt_t beta; gt_null(beta);
    bn_t v; bn_null(v);
    g1_t t1; g1_null(t1);
    gt_t t2; gt_null(t2);

    uint8_t *q_bin = NULL, *p_bin = NULL, *beta_bin = NULL, *y_bin = NULL, *t1_bin = NULL, *t2_bin = NULL;

    bn_t cpkw; bn_null(cpkw);
    bn_t vscpkw; bn_null(vscpkw);

    TRY {
        gt_new(beta);
        pc_map(beta, x, tTilde);

        scalar_mul_g1(p, g1_gen, kw, g1_ord);

        bn_new(v);

        randomZ(v, gt_ord);

        g1_new(t1);
        scalar_mul_g1(t1, g1_gen, v, g1_ord);

        gt_new(t2);
        gt_pow(t2, beta, v);

        g1_norm(t1, t1);

        int q_bin_size = g1_size_bin(g1_gen, 1);
        q_bin = calloc((size_t) q_bin_size, sizeof(uint8_t));
        serialize_g1(q_bin, q_bin_size, g1_gen);

        int p_bin_size = g1_size_bin(p, 1);
        p_bin = calloc((size_t) p_bin_size, sizeof(uint8_t));
        serialize_g1(p_bin, p_bin_size, p);

        int beta_bin_size = gt_size_bin(beta, 1);
        beta_bin = calloc((size_t) beta_bin_size, sizeof(uint8_t));
        serialize_gt(beta_bin, beta_bin_size, beta);

        int y_bin_size = gt_size_bin(y, 1);
        y_bin = calloc((size_t) y_bin_size, sizeof(uint8_t));
        serialize_gt(y_bin, y_bin_size, y);

        int t1_bin_size = g1_size_bin(t1, 1);
        t1_bin = calloc((size_t) t1_bin_size, sizeof(uint8_t));
        serialize_g1(t1_bin, t1_bin_size, t1);

        int t2_bin_size = gt_size_bin(t2, 1);
        t2_bin = calloc((size_t) t2_bin_size, sizeof(uint8_t));
        serialize_gt(t2_bin, t2_bin_size, t2);

        const uint8_t *const args[6] = {q_bin, p_bin, beta_bin, y_bin, t1_bin, t2_bin};
        const int args_sizes[6] = {q_bin_size, p_bin_size, beta_bin_size, y_bin_size, t1_bin_size,
                                   t2_bin_size};
        hashZ(c, args, 6, args_sizes);

        bn_new(cpkw);
        bn_mul(cpkw, c, kw);

        bn_new(vscpkw);
        bn_sub(vscpkw, v, cpkw);

        bn_mod(u, vscpkw, gt_ord);
    }
    CATCH_ANY {
        THROW(ERR_CAUGHT);
    }
    FINALLY {
        bn_free(vscpkw);
        bn_free(cpkw);

        free(t2_bin);
        free(t1_bin);
        free(y_bin);
        free(beta_bin);
        free(p_bin);
        free(q_bin);

        gt_free(t2);
        g1_free(t1);

        bn_free(v);

        gt_free(beta);
    }
}

void pythia_verify(int *verified, g1_t x, const uint8_t *t, int t_size, gt_t y, const g1_t p, /*IN*/ bn_t c, /*IN*/ bn_t u) {
    ep2_t tTilde; ep2_null(tTilde);
    gt_t beta; gt_null(beta);
    g1_t pc; g1_null(pc);
    g1_t qu; g1_null(qu);
    g1_t t1; g1_null(t1);
    gt_t yc; gt_null(yc);
    gt_t betau; gt_null(betau);
    gt_t t2; gt_null(t2);

    uint8_t *q_bin = NULL, *p_bin = NULL, *beta_bin = NULL, *y_bin = NULL, *t1_bin = NULL, *t2_bin = NULL;

    bn_t cPrime;

    TRY {
        ep2_new(tTilde);
        hashG2(tTilde, t, t_size);

        gt_new(beta);
        pc_map(beta, x, tTilde);

        g1_new(pc);

        scalar_mul_g1(pc, p, c, g1_ord);

        g1_new(qu);
        scalar_mul_g1(qu, g1_gen, u, g1_ord);

        g1_new(t1);
        g1_add(t1, qu, pc);

        gt_new(yc);
        gt_pow(yc, y, c);

        gt_new(betau);
        gt_pow(betau, beta, u);

        gt_new(t2);
        gt_mul(t2, betau, yc);

        g1_norm(t1, t1);

        int q_bin_size = g1_size_bin(g1_gen, 1);
        q_bin = calloc((size_t) q_bin_size, sizeof(uint8_t));
        serialize_g1(q_bin, q_bin_size, g1_gen);

        int p_bin_size = g1_size_bin(p, 1);
        p_bin = calloc((size_t) p_bin_size, sizeof(uint8_t));
        serialize_g1(p_bin, p_bin_size, p);

        int beta_bin_size = gt_size_bin(beta, 1);
        beta_bin = calloc((size_t) beta_bin_size, sizeof(uint8_t));
        serialize_gt(beta_bin, beta_bin_size, beta);

        int y_bin_size = gt_size_bin(y, 1);
        y_bin = calloc((size_t) y_bin_size, sizeof(uint8_t));
        serialize_gt(y_bin, y_bin_size, y);

        int t1_bin_size = g1_size_bin(t1, 1);
        t1_bin = calloc((size_t) t1_bin_size, sizeof(uint8_t));
        serialize_g1(t1_bin, t1_bin_size, t1);

        int t2_bin_size = gt_size_bin(t2, 1);
        t2_bin = calloc((size_t) t2_bin_size, sizeof(uint8_t));
        serialize_gt(t2_bin, t2_bin_size, t2);

        const uint8_t *const args[6] = {q_bin, p_bin, beta_bin, y_bin, t1_bin, t2_bin};
        const int args_sizes[6] = {q_bin_size, p_bin_size, beta_bin_size, y_bin_size, t1_bin_size,
                                   t2_bin_size};

        bn_new(cPrime);
        hashZ(cPrime, args, 6, args_sizes);

        *verified = bn_cmp(cPrime, c) == CMP_EQ;
    }
    CATCH_ANY {
        THROW(ERR_CAUGHT);
    }
    FINALLY {
        bn_free(cPrime)

        free(q_bin);
        free(p_bin);
        free(beta_bin);
        free(y_bin);
        free(t1_bin);
        free(t2_bin);

        gt_free(t2);
        gt_free(betau);
        gt_free(yc);
        g1_free(t1);
        g1_free(qu);
        g1_free(pc);
        gt_free(beta);
        ep2_free(tTilde);
    }
}

void pythia_get_delta(bn_t delta, g1_t pPrime,
           const uint8_t *w0, int w_size0, const uint8_t *msk0, int msk_size0, const uint8_t *z0, int z_size0,
           const uint8_t *w1, int w_size1, const uint8_t *msk1, int msk_size1, const uint8_t *z1, int z_size1) {
    bn_t kw1; bn_null(kw1);
    bn_t kw0; bn_null(kw0);
    bn_t kw0Inv; bn_null(kw0Inv);
    bn_t gcd; bn_null(gcd);
    bn_t kw1kw0Inv; bn_null(kw1kw0Inv);

    TRY {
        bn_new(kw1);
        genKw(kw1, w1, w_size1, msk1, msk_size1, z1, z_size1);

        bn_new(kw0);
        genKw(kw0, w0, w_size0, msk0, msk_size0, z0, z_size0);

        bn_new(kw0Inv);

        bn_new(gcd);

        bn_gcd_ext(gcd, kw0Inv, NULL, kw0, gt_ord);

        bn_new(kw1kw0Inv);
        bn_mul(kw1kw0Inv, kw1, kw0Inv);

        bn_mod(delta, kw1kw0Inv, gt_ord);

        g1_mul_gen(pPrime, kw1);
    }
    CATCH_ANY {
        THROW(ERR_CAUGHT);
    }
    FINALLY {
        bn_free(kw1kw0Inv);
        bn_free(gcd);
        bn_free(kw0Inv);
        bn_free(kw0);
        bn_free(kw1);
    }
}

void pythia_update(gt_t r, gt_t z, /*IN*/ bn_t delta) {
    TRY {
        gt_pow(r, z, delta);
    }
    CATCH_ANY {
        THROW(ERR_CAUGHT);
    }
    FINALLY {}
}