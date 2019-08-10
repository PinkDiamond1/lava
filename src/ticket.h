#ifndef BITCOIN_TICKET_H
#define BITCOIN_TICKET_H

#include <script/script.h>
#include <pubkey.h>
#include <amount.h>

#include <functional>

CScript GenerateTicketScript(const CKeyID keyid, const int lockHeight);

bool DecodeTicketScript(const CScript redeemScript, CKeyID& keyID, int &lockHeight);

bool GetPublicKeyFromScript(const CScript script, CPubKey& pubkey);
bool GetRedeemFromScript(const CScript script, CScript& redeemscript);

class CTicket {
public:
    static const int32_t VERSION = 1;

    enum CTicketState {
        IMMATURATE = 0,
        USEABLE,
        OVERDUE,
        UNKNOW
    };

    CTicket(const uint256& txid, const uint32_t n, const CAmount nValue, const CScript& redeemScript, const CScript &scriptPubkey);

    CTicket() = default;
    ~CTicket() = default;

    CTicketState State(int activeHeight) const;

    int LockTime()const;

    CKeyID KeyID() const;

    bool Invalid() const;

    const uint256& GetHash() const { return hash; }
    const uint256& GetTxHash() const { return txid; }
    uint32_t GetIndex() const { return n; }
    const CScript& GetRedeemScript() const { return redeemScript; }
    const CScript& GetScriptPubkey() const { return scriptPubkey; }
    const CAmount& Amount() const { return nValue; }
    void setValue(uint256 ticketid, uint256 txid, uint32_t n, CAmount nValue, CScript redeemscript, CScript scriptpubkey){
	    this->hash = ticketid;
	    this->txid = txid;
	    this->n = n;
    this->nValue = nValue;
	    this->redeemScript = redeemscript;
	    this->scriptPubkey = scriptpubkey;
    }

    template <typename Stream>
    inline void Serialize(Stream& s) const {
	    s << txid;
	    s << n;
	    s << redeemScript;
    }

    template <typename Stream>
    inline void Unserialize(Stream& s) {
	    s >> txid;
	    s >> n;
	    s >> redeemScript;
    }
	
private:
    // only memory
   uint256 hash; //hash(txid, n, redeemScript)
   uint256 txid;
   CAmount nValue;
   uint32_t n;
   CScript redeemScript;
   CScript scriptPubkey;
   uint256 ComputeHash() const;
};

typedef std::shared_ptr<const CTicket> CTicketRef;
class CBlock;
typedef std::function<bool(const int, const CTicketRef&)> CheckTicketFunc;

class CTicketView {
public:
    CTicketView();
    ~CTicketView() = default;

    void ConncetBlock(const int height, const CBlock &blk, CheckTicketFunc checkTicket);

    void DisconnectBlock(const int height, const CBlock &blk);

    CAmount CurrentTicketPrice() const;

    std::vector<CTicketRef> CurrentSlotTicket();

    const int SlotIndex() const { return slotIndex; }

    const int SlotLenght();

    const int LockTime();
private:
    std::map<int, std::vector<CTicketRef>> ticketsInSlot;
    std::map<CKeyID, std::vector<CTicketRef>> ticketsInAddr;
    CAmount ticketPrice;
    int slotIndex;
    static CAmount BaseTicketPrice;
};

#endif
