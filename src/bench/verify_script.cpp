// Copyright (c) 2016-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bench/bench.h>
#include <key.h>
#if defined(HAVE_CONSENSUS_LIB)
#include <script/bitcoinconsensus.h>
#endif
#include <script/script.h>
#include <script/standard.h>
#include <streams.h>
#include <test/util/transaction_utils.h>

#include <array>

// Microbenchmark for verification of a basic P2WPKH script. Can be easily
// modified to measure performance of other types of scripts.
static void VerifyScriptBench(benchmark::State& state)
{
    const int flags = SCRIPT_VERIFY_P2SH;

    // Keypair.
    CKey key;
    static const std::array<unsigned char, 32> vchKey = {
        {
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1
        }
    };
    key.Set(vchKey.begin(), vchKey.end(), false);
    CPubKey pubkey = key.GetPubKey();
    uint160 pubkeyHash;
    CHash160().Write(pubkey.begin(), pubkey.size()).Finalize(pubkeyHash.begin());

    // Script.
    CScript scriptPubKey = CScript() << ToByteVector(pubkeyHash);
    CScript scriptSig;
    CScript txScriptPubkey = CScript() << OP_DUP << OP_HASH160 << ToByteVector(pubkeyHash) << OP_EQUALVERIFY << OP_CHECKSIG;
    const CMutableTransaction& txCredit = BuildCreditingTransaction(scriptPubKey, 1);
    CMutableTransaction txSpend = BuildSpendingTransaction(scriptSig, CTransaction(txCredit));
    key.Sign(SignatureHash(txScriptPubkey, txSpend, 0, SIGHASH_ALL, txCredit.vout[0].nValue, SigVersion::BASE), 0);

    // Benchmark.
    while (state.KeepRunning()) {
        ScriptError err;
        bool success = VerifyScript(
            txSpend.vin[0].scriptSig,
            txCredit.vout[0].scriptPubKey,
            flags,
            MutableTransactionSignatureChecker(&txSpend, 0, txCredit.vout[0].nValue),
            &err);
        assert(err == SCRIPT_ERR_OK);
        assert(success);

#if defined(HAVE_CONSENSUS_LIB)
        CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
        stream << txSpend;
        int csuccess = bitcoinconsensus_verify_script_with_amount(
            txCredit.vout[0].scriptPubKey.data(),
            txCredit.vout[0].scriptPubKey.size(),
            txCredit.vout[0].nValue,
            (const unsigned char*)stream.data(), stream.size(), 0, flags, nullptr);
        assert(csuccess == 1);
#endif
    }
}

static void VerifyNestedIfScript(benchmark::State& state) {
    std::vector<std::vector<unsigned char>> stack;
    CScript script;
    for (int i = 0; i < 100; ++i) {
        script << OP_1 << OP_IF;
    }
    for (int i = 0; i < 1000; ++i) {
        script << OP_1;
    }
    for (int i = 0; i < 100; ++i) {
        script << OP_ENDIF;
    }
    while (state.KeepRunning()) {
        auto stack_copy = stack;
        ScriptError error;
        bool ret = EvalScript(stack_copy, script, 0, BaseSignatureChecker(), SigVersion::BASE, &error);
        assert(ret);
    }
}


BENCHMARK(VerifyScriptBench, 6300);

BENCHMARK(VerifyNestedIfScript, 100);
