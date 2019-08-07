#include <actiondb.h>
#include <validation.h>
#include <chainparams.h>
#include <logging.h>

CAction MakeBindAction(const CKeyID& from, const CKeyID& to)
{
    CBindAction ba(std::make_pair(from, to));
    return std::move(CAction(ba));
}

bool SignAction(const CAction &action, const CKey& key, std::vector<unsigned char>& vch)
{
    vch.clear();
    auto actionVch = SerializeAction(action);
    vch.insert(vch.end(), actionVch.begin(), actionVch.end());
    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << actionVch;
    std::vector<unsigned char> vchSig;
    if (!key.SignCompact(ss.GetHash(), vchSig)) {
        return false;
    }
    vch.insert(vch.end(), vchSig.begin(), vchSig.end());
    return true;
}

bool VerifyAction(const CAction& action, std::vector<unsigned char>& vchSig)
{
    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << SerializeAction(action);
    CPubKey pubkey;
    if (!pubkey.RecoverCompact(ss.GetHash(), vchSig))
        return false;
    auto result{ false };
    if (action.type() == typeid(CBindAction)) {
        auto from = boost::get<CBindAction>(action).first;
        result = from == pubkey.GetID();
    } else if (action.type() == typeid(CUnbindAction)) {
        auto from = boost::get<CUnbindAction>(action);
        result = from == pubkey.GetID();
    }
    return result;
}

std::vector<unsigned char> SerializeAction(const CAction& action) {
    CDataStream ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << action.which();
    boost::apply_visitor(CActionVisitor(&ss), action);
    return std::vector<unsigned char>(ss.begin(), ss.end());
}

CAction UnserializeAction(const std::vector<unsigned char>& vch) {
    CDataStream ss(vch, SER_GETHASH, PROTOCOL_VERSION);
    int ty = 0;
    ss >> ty;
    switch (ty) {
    case 1:
    {
        CBindAction ba;
        ss >> ba;
        return std::move(CAction(ba));
    }
    case 2:
    {
        CUnbindAction uba;
        ss >> uba;
        return std::move(CAction(uba));
    }
    }
    return std::move(CAction(CNilAction{}));
}

CAction DecodeAction(const CTransactionRef tx, std::vector<unsigned char>& vchSig)
{
    do {
        if (tx->IsCoinBase() || tx->IsNull() || tx->vout.size() != 2 
            || (tx->vout[0].nValue != 0 && tx->vout[1].nValue != 0)) 
            continue;

        CAmount nAmount{ 0 };
        for (auto vin : tx->vin) {
            auto coin = pcoinsTip->AccessCoin(vin.prevout);
            nAmount += coin.out.nValue;
        }
        auto outValue = tx->GetValueOut();
        if (nAmount - outValue != Params().GetConsensus().nActionFee) {
            LogPrintf("Action error fees, fee=%u\n", nAmount - outValue);
            continue;
        }
        for (auto vout : tx->vout) {
            if (vout.nValue != 0) continue;
            auto script = vout.scriptPubKey;
            CScriptBase::const_iterator pc = script.begin();
            opcodetype opcodeRet;
            std::vector<unsigned char> vchRet;
            if (!script.GetOp(pc, opcodeRet, vchRet) || opcodeRet != OP_RETURN) {
                continue;
            }
            script.GetOp(pc, opcodeRet, vchRet);
            auto action = UnserializeAction(vchRet);
            if (vchRet.size() < 65) continue;
            vchSig.clear();
            vchSig.insert(vchSig.end(), vchRet.end() - 65, vchRet.end());
            return std::move(action);
        }
    } while (false);
    return CAction(CNilAction{});
}

std::unique_ptr<CRelationDB> g_relationdb;

static const char DB_RELATION_KEY = 'K';
static const char DB_ACTIVE_ACTION_KEY = 'A';

CRelationDB::CRelationDB(size_t nCacheSize, bool fMemory, bool fWipe)
    : CDBWrapper(GetDataDir() / "index" / "relation", nCacheSize, fMemory, fWipe) 
{
}

bool CRelationDB::InsertRelation(const CKeyID& from, const CKeyID& to)
{
    return Write(std::make_pair(DB_RELATION_KEY, from), to);
}

CKeyID CRelationDB::To(const CKeyID& from) const
{
    auto to = To(from.GetPlotID());
    return std::move(to);
}

CKeyID CRelationDB::To(const uint64_t plotid) const
{
    auto value = std::make_pair(CKeyID(), CKeyID());
    if (!Read(std::make_pair(DB_RELATION_KEY, plotid), value)) {
        LogPrint(BCLog::DB, "CRelationDB::To failure, get bind to, from:%u\n", plotid);
    }
    return std::move(value.second);
}

typedef std::pair<CKeyID, CKeyID> CRelationActive;

bool CRelationDB::AcceptAction(const uint256& txid, const CAction& action)
{
    LogPrintf("AcceptAction, tx:%s\n", txid.GetHex());
    CDBBatch batch(*this);
    if (action.type() == typeid(CBindAction)) {
        auto ba = boost::get<CBindAction>(action);
        auto from = ba.first;
        auto nowTo = To(from);
        CRelationActive active{ std::make_pair(from, nowTo) };
        batch.Write(std::make_pair(DB_ACTIVE_ACTION_KEY, txid), active);
        batch.Write(std::make_pair(DB_RELATION_KEY, from.GetPlotID()), std::make_pair(ba.first, ba.second));
        LogPrintf("bind action, from:%u, to:%u\n", from.GetPlotID(), ba.second.GetPlotID());
    } else if (action.type() == typeid(CUnbindAction)) {
        auto from = boost::get<CUnbindAction>(action);
        auto nowTo = To(from);
        CRelationActive active{ std::make_pair(from, nowTo) };
        LogPrintf("unbind action, from:%u\n", from.GetPlotID());
        batch.Write(std::make_pair(DB_ACTIVE_ACTION_KEY, txid), active);
        batch.Erase(std::make_pair(DB_RELATION_KEY, from));
    }
    return WriteBatch(batch, true);
}

bool CRelationDB::RollbackAction(const uint256& txid)
{
    CRelationActive active;
    auto activeKey = std::make_pair(DB_ACTIVE_ACTION_KEY, txid);
    if (!Read(activeKey, active))
        return false;
    CDBBatch batch(*this);
    batch.Erase(activeKey);
    auto relKey = std::make_pair(DB_RELATION_KEY, active.first.GetPlotID());
    if (active.second != CKeyID()) {
        batch.Write(relKey, active.second);
    } else if (Exists(relKey)) {
        batch.Erase(relKey);
    }
    return WriteBatch(batch, true);
}

CRelationVector CRelationDB::ListRelations() const
{
    CRelationVector vch;
    std::unique_ptr<CDBIterator> iter(const_cast<CRelationDB&>(*this).NewIterator());
    iter->Seek(std::make_pair(DB_RELATION_KEY, 0));
    while (iter->Valid()) {
        auto value = std::make_pair(CKeyID(), CKeyID());
        iter->GetValue(value);
        vch.push_back(value);
        iter->Next();
    }
    return std::move(vch);
}