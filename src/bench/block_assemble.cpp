// Copyright (c) 2011-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bench/bench.h>
#include <chainparams.h>
#include <coins.h>
#include <consensus/merkle.h>
#include <consensus/validation.h>
#include <miner.h>
#include <policy/policy.h>
#include <pow.h>
#include <scheduler.h>
#include <txdb.h>
#include <txmempool.h>
#include <utiltime.h>
#include <validation.h>
#include <validationinterface.h>

#include <boost/thread.hpp>

#include <list>
#include <vector>

static std::shared_ptr<CBlock> PrepareBlock(const CScript& coinbase_scriptPubKey)
{
    auto block = std::make_shared<CBlock>(
        BlockAssembler{Params()}
            .CreateNewBlock(coinbase_scriptPubKey)
            ->block);

    block->nTime = ::chainActive.Tip()->GetPastTimeLimit() + 1;
    block->hashMerkleRoot = BlockMerkleRoot(*block);

    return block;
}


static CTxIn MineBlock(const CScript& coinbase_scriptPubKey)
{
    auto block = PrepareBlock(coinbase_scriptPubKey);

    while (!CheckProofOfWork(block->GetHash(), block->nBits, Params().GetConsensus())) {
        assert(++block->nNonce);
    }

    bool processed{ProcessNewBlock(Params(), block, true, nullptr)};
    assert(processed);

    return CTxIn{block->vtx[0]->GetHash(), 0};
}


static void AssembleBlock(benchmark::State& state)
{
    const CScript redeemScript = CScript() << OP_DROP << OP_TRUE;
    const CScript SCRIPT_PUB =
        CScript() << OP_HASH160 << ToByteVector(CScriptID(redeemScript))
                  << OP_EQUAL;

    const CScript scriptSig = CScript() << std::vector<uint8_t>(100, 0xff)
                                        << ToByteVector(redeemScript);
							
    // Switch to regtest so we can mine faster
    SelectParams(CBaseChainParams::REGTEST);

    InitScriptExecutionCache();

    boost::thread_group thread_group;
    CScheduler scheduler;
    {
        ::pblocktree.reset(new CBlockTreeDB(1 << 20, true));
        ::pcoinsdbview.reset(new CCoinsViewDB(1 << 23, true));
        ::pcoinsTip.reset(new CCoinsViewCache(pcoinsdbview.get()));

        const CChainParams& chainparams = Params();
        thread_group.create_thread(boost::bind(&CScheduler::serviceQueue, &scheduler));
        GetMainSignals().RegisterBackgroundSignalScheduler(scheduler);
        LoadGenesisBlock(chainparams);
        CValidationState state;
        ActivateBestChain(state, chainparams);
        assert(::chainActive.Tip() != nullptr);
    }

    // Collect some loose transactions that spend the coinbases of our mined blocks
    constexpr size_t NUM_BLOCKS{600};
	constexpr int COINBASE_MATURITY = 500;
    std::array<CTransactionRef, NUM_BLOCKS - COINBASE_MATURITY + 1> txs;
    for (size_t b{0}; b < NUM_BLOCKS; ++b) {
        CMutableTransaction tx;
        tx.vin.push_back(MineBlock(SCRIPT_PUB));
        tx.vin.back().scriptSig = scriptSig;
        tx.vout.emplace_back(1337, SCRIPT_PUB);
        if (NUM_BLOCKS - b >= COINBASE_MATURITY)
            txs.at(b) = MakeTransactionRef(tx);
    }
    {
        LOCK(::cs_main); // Required for ::AcceptToMemoryPool.

        for (const auto& txr : txs) {
            CValidationState state;
            bool ret{::AcceptToMemoryPool(::mempool, state, txr, nullptr /* pfMissingInputs */, nullptr /* plTxnReplaced */, false /* bypass_limits */, /* nAbsurdFee */ 0)};
            assert(ret);
        }
    }

    while (state.KeepRunning()) {
        PrepareBlock(SCRIPT_PUB);
    }

    thread_group.interrupt_all();
    thread_group.join_all();
    GetMainSignals().FlushBackgroundCallbacks();
    GetMainSignals().UnregisterBackgroundSignalScheduler();
}

BENCHMARK(AssembleBlock, 700);
