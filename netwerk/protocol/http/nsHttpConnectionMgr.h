/* vim:set ts=4 sw=4 sts=4 et cin: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsHttpConnectionMgr_h__
#define nsHttpConnectionMgr_h__

#include "nsHttpConnectionInfo.h"
#include "nsHttpConnection.h"
#include "nsHttpTransaction.h"
#include "NullHttpTransaction.h"
#include "nsTArray.h"
#include "nsThreadUtils.h"
#include "nsClassHashtable.h"
#include "nsDataHashtable.h"
#include "nsAutoPtr.h"
#include "mozilla/ReentrantMonitor.h"
#include "nsISocketTransportService.h"
#include "mozilla/TimeStamp.h"

#include "nsIObserver.h"
#include "nsITimer.h"
#include "nsIX509Cert3.h"

class nsHttpPipeline;

class nsIHttpUpgradeListener;

//-----------------------------------------------------------------------------

class nsHttpConnectionMgr : public nsIObserver
{
public:
    NS_DECL_ISUPPORTS
    NS_DECL_NSIOBSERVER

    // parameter names
    enum nsParamName {
        MAX_CONNECTIONS,
        MAX_CONNECTIONS_PER_HOST,
        MAX_CONNECTIONS_PER_PROXY,
        MAX_PERSISTENT_CONNECTIONS_PER_HOST,
        MAX_PERSISTENT_CONNECTIONS_PER_PROXY,
        MAX_REQUEST_DELAY,
        MAX_PIPELINED_REQUESTS,
        MAX_OPTIMISTIC_PIPELINED_REQUESTS
    };

    //-------------------------------------------------------------------------
    // NOTE: functions below may only be called on the main thread.
    //-------------------------------------------------------------------------

    nsHttpConnectionMgr();

    nsresult Init(PRUint16 maxConnections,
                  PRUint16 maxConnectionsPerHost,
                  PRUint16 maxConnectionsPerProxy,
                  PRUint16 maxPersistentConnectionsPerHost,
                  PRUint16 maxPersistentConnectionsPerProxy,
                  PRUint16 maxRequestDelay,
                  PRUint16 maxPipelinedRequests,
                  PRUint16 maxOptimisticPipelinedRequests);
    nsresult Shutdown();

    //-------------------------------------------------------------------------
    // NOTE: functions below may be called on any thread.
    //-------------------------------------------------------------------------

    // Schedules next pruning of dead connection to happen after
    // given time.
    void PruneDeadConnectionsAfter(PRUint32 time);

    // Stops timer scheduled for next pruning of dead connections if
    // there are no more idle connections or active spdy ones
    void ConditionallyStopPruneDeadConnectionsTimer();

    // Stops timer used for the read timeout tick if there are no currently
    // active connections.
    void ConditionallyStopReadTimeoutTick();

    // adds a transaction to the list of managed transactions.
    nsresult AddTransaction(nsHttpTransaction *, PRInt32 priority);

    // called to reschedule the given transaction.  it must already have been
    // added to the connection manager via AddTransaction.
    nsresult RescheduleTransaction(nsHttpTransaction *, PRInt32 priority);

    // cancels a transaction w/ the given reason.
    nsresult CancelTransaction(nsHttpTransaction *, nsresult reason);

    // called to force the connection manager to prune its list of idle
    // connections.
    nsresult PruneDeadConnections();

    // Close all idle persistent connections and prevent any active connections
    // from being reused.
    nsresult ClosePersistentConnections();

    // called to get a reference to the socket transport service.  the socket
    // transport service is not available when the connection manager is down.
    nsresult GetSocketThreadTarget(nsIEventTarget **);

    // called to indicate a transaction for the connectionInfo is likely coming
    // soon. The connection manager may use this information to start a TCP
    // and/or SSL level handshake for that resource immediately so that it is
    // ready when the transaction is submitted. No obligation is taken on by the
    // connection manager, nor is the submitter obligated to actually submit a
    // real transaction for this connectionInfo.
    nsresult SpeculativeConnect(nsHttpConnectionInfo *,
                                nsIInterfaceRequestor *,
                                nsIEventTarget *);

    // called when a connection is done processing a transaction.  if the 
    // connection can be reused then it will be added to the idle list, else
    // it will be closed.
    nsresult ReclaimConnection(nsHttpConnection *conn);

    // called by the main thread to execute the taketransport() logic on the
    // socket thread after a 101 response has been received and the socket
    // needs to be transferred to an expectant upgrade listener such as
    // websockets.
    nsresult CompleteUpgrade(nsAHttpConnection *aConn,
                             nsIHttpUpgradeListener *aUpgradeListener);

    // called to update a parameter after the connection manager has already
    // been initialized.
    nsresult UpdateParam(nsParamName name, PRUint16 value);

    // Lookup/Cancel HTTP->SPDY redirections
    bool GetSpdyAlternateProtocol(nsACString &key);
    void ReportSpdyAlternateProtocol(nsHttpConnection *);
    void RemoveSpdyAlternateProtocol(nsACString &key);

    // Pipielining Interfaces and Datatypes

    const static PRUint32 kPipelineInfoTypeMask = 0xffff0000;
    const static PRUint32 kPipelineInfoIDMask   = ~kPipelineInfoTypeMask;

    const static PRUint32 kPipelineInfoTypeRed     = 0x00010000;
    const static PRUint32 kPipelineInfoTypeBad     = 0x00020000;
    const static PRUint32 kPipelineInfoTypeNeutral = 0x00040000;
    const static PRUint32 kPipelineInfoTypeGood    = 0x00080000;

    enum PipelineFeedbackInfoType
    {
        // Used when an HTTP response less than 1.1 is received
        RedVersionTooLow = kPipelineInfoTypeRed | kPipelineInfoTypeBad | 0x0001,

        // Used when a HTTP Server response header that is on the banned from
        // pipelining list is received
        RedBannedServer = kPipelineInfoTypeRed | kPipelineInfoTypeBad | 0x0002,
    
        // Used when a response is terminated early, when it fails an
        // integrity check such as assoc-req or when a 304 contained a Last-Modified
        // differnet than the entry being validated.
        RedCorruptedContent = kPipelineInfoTypeRed | kPipelineInfoTypeBad | 0x0004,

        // Used when a pipeline is only partly satisfied - for instance if the
        // server closed the connection after responding to the first
        // request but left some requests unprocessed.
        RedCanceledPipeline = kPipelineInfoTypeRed | kPipelineInfoTypeBad | 0x0005,

        // Used when a connection that we expected to stay persistently open
        // was closed by the server. Not used when simply timed out.
        BadExplicitClose = kPipelineInfoTypeBad | 0x0003,

        // Used when there is a gap of around 400 - 1200ms in between data being
        // read from the server
        BadSlowReadMinor = kPipelineInfoTypeBad | 0x0006,

        // Used when there is a gap of > 1200ms in between data being
        // read from the server
        BadSlowReadMajor = kPipelineInfoTypeBad | 0x0007,

        // Used when a response is received that is not framed with either chunked
        // encoding or a complete content length.
        BadInsufficientFraming = kPipelineInfoTypeBad | 0x0008,
        
        // Used when a very large response is recevied in a potential pipelining
        // context. Large responses cause head of line blocking.
        BadUnexpectedLarge = kPipelineInfoTypeBad | 0x000B,

        // Used when a response is received that has headers that appear to support
        // pipelining.
        NeutralExpectedOK = kPipelineInfoTypeNeutral | 0x0009,

        // Used when a response is received successfully to a pipelined request.
        GoodCompletedOK = kPipelineInfoTypeGood | 0x000A
    };
    
    // called to provide information relevant to the pipelining manager
    // may be called from any thread
    void     PipelineFeedbackInfo(nsHttpConnectionInfo *,
                                  PipelineFeedbackInfoType info,
                                  nsHttpConnection *,
                                  PRUint32);

    void ReportFailedToProcess(nsIURI *uri);

    // Causes a large amount of connection diagnostic information to be
    // printed to the javascript console
    void PrintDiagnostics();

    //-------------------------------------------------------------------------
    // NOTE: functions below may be called only on the socket thread.
    //-------------------------------------------------------------------------

    // called to force the transaction queue to be processed once more, giving
    // preference to the specified connection.
    nsresult ProcessPendingQ(nsHttpConnectionInfo *);
    bool     ProcessPendingQForEntry(nsHttpConnectionInfo *);

    // This is used to force an idle connection to be closed and removed from
    // the idle connection list. It is called when the idle connection detects
    // that the network peer has closed the transport.
    nsresult CloseIdleConnection(nsHttpConnection *);

    // The connection manager needs to know when a normal HTTP connection has been
    // upgraded to SPDY because the dispatch and idle semantics are a little
    // bit different.
    void ReportSpdyConnection(nsHttpConnection *, bool usingSpdy);

    
    bool     SupportsPipelining(nsHttpConnectionInfo *);

private:
    virtual ~nsHttpConnectionMgr();

    enum PipeliningState {
        // Host has proven itself pipeline capable through past experience and
        // large pipeline depths are allowed on multiple connections.
        PS_GREEN,

        // Not enough information is available yet with this host to be certain
        // of pipeline capability. Small pipelines on a single connection are
        // allowed in order to decide whether or not to proceed to green.
        PS_YELLOW,

        // One or more bad events has happened that indicate that pipelining
        // to this host (or a particular type of transaction with this host)
        // is a bad idea. Pipelining is not currently allowed, but time and
        // other positive experiences will eventually allow it to try again.
        PS_RED
    };
    
    class nsHalfOpenSocket;

    // nsConnectionEntry
    //
    // mCT maps connection info hash key to nsConnectionEntry object, which
    // contains list of active and idle connections as well as the list of
    // pending transactions.
    //
    class nsConnectionEntry
    {
    public:
        nsConnectionEntry(nsHttpConnectionInfo *ci);
        ~nsConnectionEntry();

        nsHttpConnectionInfo        *mConnInfo;
        nsTArray<nsHttpTransaction*> mPendingQ;    // pending transaction queue
        nsTArray<nsHttpConnection*>  mActiveConns; // active connections
        nsTArray<nsHttpConnection*>  mIdleConns;   // idle persistent connections
        nsTArray<nsHalfOpenSocket*>  mHalfOpens;

        // Pipeline depths for various states
        const static PRUint32 kPipelineUnlimited  = 1024; // fully open - extended green
        const static PRUint32 kPipelineOpen       = 6;    // 6 on each conn - normal green
        const static PRUint32 kPipelineRestricted = 2;    // 2 on just 1 conn in yellow
        
        nsHttpConnectionMgr::PipeliningState PipelineState();
        void OnPipelineFeedbackInfo(
            nsHttpConnectionMgr::PipelineFeedbackInfoType info,
            nsHttpConnection *, PRUint32);
        bool SupportsPipelining();
        PRUint32 MaxPipelineDepth(nsAHttpTransaction::Classifier classification);
        void CreditPenalty();

        nsHttpConnectionMgr::PipeliningState mPipelineState;

        void SetYellowConnection(nsHttpConnection *);
        void OnYellowComplete();
        PRUint32                  mYellowGoodEvents;
        PRUint32                  mYellowBadEvents;
        nsHttpConnection         *mYellowConnection;

        // initialGreenDepth is the max depth of a pipeline when you first
        // transition to green. Normally this is kPipelineOpen, but it can
        // be kPipelineUnlimited in aggressive mode.
        PRUint32                  mInitialGreenDepth;

        // greenDepth is the current max allowed depth of a pipeline when
        // in the green state. Normally this starts as kPipelineOpen and
        // grows to kPipelineUnlimited after a pipeline of depth 3 has been
        // successfully transacted.
        PRUint32                  mGreenDepth;

        // pipeliningPenalty is the current amount of penalty points this host
        // entry has earned for participating in events that are not conducive
        // to good pipelines - such as head of line blocking, canceled pipelines,
        // etc.. penalties are paid back either through elapsed time or simply
        // healthy transactions. Having penalty points means that this host is
        // not currently eligible for pipelines.
        PRInt16                   mPipeliningPenalty;

        // some penalty points only apply to particular classifications of
        // transactions - this allows a server that perhaps has head of line
        // blocking problems on CGI queries to still serve JS pipelined.
        PRInt16                   mPipeliningClassPenalty[nsAHttpTransaction::CLASS_MAX];

        // for calculating penalty repair credits
        mozilla::TimeStamp        mLastCreditTime;

        // Spdy sometimes resolves the address in the socket manager in order
        // to re-coalesce sharded HTTP hosts. The dotted decimal address is
        // combined with the Anonymous flag from the connection information
        // to build the hash key for hosts in the same ip pool.
        //
        // When a set of hosts are coalesced together one of them is marked
        // mSpdyPreferred. The mapping is maintained in the connection mananger
        // mSpdyPreferred hash.
        //
        nsCString mCoalescingKey;

        // To have the UsingSpdy flag means some host with the same connection
        // entry has done NPN=spdy/* at some point. It does not mean every
        // connection is currently using spdy.
        bool mUsingSpdy;

        // mTestedSpdy is set after NPN negotiation has occurred and we know
        // with confidence whether a host speaks spdy or not (which is reflected
        // in mUsingSpdy). Before mTestedSpdy is set, handshake parallelism is
        // minimized so that we can multiplex on a single spdy connection.
        bool mTestedSpdy;

        bool mSpdyPreferred;
    };

    // nsConnectionHandle
    //
    // thin wrapper around a real connection, used to keep track of references
    // to the connection to determine when the connection may be reused.  the
    // transaction (or pipeline) owns a reference to this handle.  this extra
    // layer of indirection greatly simplifies consumer code, avoiding the
    // need for consumer code to know when to give the connection back to the
    // connection manager.
    //
    class nsConnectionHandle : public nsAHttpConnection
    {
    public:
        NS_DECL_ISUPPORTS
        NS_DECL_NSAHTTPCONNECTION(mConn)

        nsConnectionHandle(nsHttpConnection *conn) { NS_ADDREF(mConn = conn); }
        virtual ~nsConnectionHandle();

        nsHttpConnection *mConn;
    };

    // nsHalfOpenSocket is used to hold the state of an opening TCP socket
    // while we wait for it to establish and bind it to a connection

    class nsHalfOpenSocket : public nsIOutputStreamCallback,
                             public nsITransportEventSink,
                             public nsIInterfaceRequestor,
                             public nsITimerCallback
    {
    public:
        NS_DECL_ISUPPORTS
        NS_DECL_NSIOUTPUTSTREAMCALLBACK
        NS_DECL_NSITRANSPORTEVENTSINK
        NS_DECL_NSIINTERFACEREQUESTOR
        NS_DECL_NSITIMERCALLBACK

        nsHalfOpenSocket(nsConnectionEntry *ent,
                         nsAHttpTransaction *trans,
                         PRUint8 caps);
        ~nsHalfOpenSocket();
        
        nsresult SetupStreams(nsISocketTransport **,
                              nsIAsyncInputStream **,
                              nsIAsyncOutputStream **,
                              bool isBackup);
        nsresult SetupPrimaryStreams();
        nsresult SetupBackupStreams();
        void     SetupBackupTimer();
        void     CancelBackupTimer();
        void     Abandon();
        
        nsAHttpTransaction *Transaction() { return mTransaction; }

        bool IsSpeculative() { return mSpeculative; }
        void SetSpeculative(bool val) { mSpeculative = val; }

        bool HasConnected() { return mHasConnected; }

        void PrintDiagnostics(nsCString &log);
    private:
        nsConnectionEntry              *mEnt;
        nsRefPtr<nsAHttpTransaction>   mTransaction;
        nsCOMPtr<nsISocketTransport>   mSocketTransport;
        nsCOMPtr<nsIAsyncOutputStream> mStreamOut;
        nsCOMPtr<nsIAsyncInputStream>  mStreamIn;
        PRUint8                        mCaps;

        // mSpeculative is set if the socket was created from
        // SpeculativeConnect(). It is cleared when a transaction would normally
        // start a new connection from scratch but instead finds this one in
        // the half open list and claims it for its own use. (which due to
        // the vagaries of scheduling from the pending queue might not actually
        // match up - but it prevents a speculative connection from opening
        // more connections that are needed.)
        bool                           mSpeculative;

        mozilla::TimeStamp             mPrimarySynStarted;
        mozilla::TimeStamp             mBackupSynStarted;

        // for syn retry
        nsCOMPtr<nsITimer>             mSynTimer;
        nsCOMPtr<nsISocketTransport>   mBackupTransport;
        nsCOMPtr<nsIAsyncOutputStream> mBackupStreamOut;
        nsCOMPtr<nsIAsyncInputStream>  mBackupStreamIn;

        bool                           mHasConnected;
    };
    friend class nsHalfOpenSocket;

    //-------------------------------------------------------------------------
    // NOTE: these members may be accessed from any thread (use mReentrantMonitor)
    //-------------------------------------------------------------------------

    PRInt32                      mRef;
    mozilla::ReentrantMonitor    mReentrantMonitor;
    nsCOMPtr<nsIEventTarget>     mSocketThreadTarget;

    // connection limits
    PRUint16 mMaxConns;
    PRUint16 mMaxConnsPerHost;
    PRUint16 mMaxConnsPerProxy;
    PRUint16 mMaxPersistConnsPerHost;
    PRUint16 mMaxPersistConnsPerProxy;
    PRUint16 mMaxRequestDelay; // in seconds
    PRUint16 mMaxPipelinedRequests;
    PRUint16 mMaxOptimisticPipelinedRequests;
    bool mIsShuttingDown;

    //-------------------------------------------------------------------------
    // NOTE: these members are only accessed on the socket transport thread
    //-------------------------------------------------------------------------

    static PLDHashOperator ProcessOneTransactionCB(const nsACString &, nsAutoPtr<nsConnectionEntry> &, void *);

    static PLDHashOperator PruneDeadConnectionsCB(const nsACString &, nsAutoPtr<nsConnectionEntry> &, void *);
    static PLDHashOperator ShutdownPassCB(const nsACString &, nsAutoPtr<nsConnectionEntry> &, void *);
    static PLDHashOperator PurgeExcessIdleConnectionsCB(const nsACString &, nsAutoPtr<nsConnectionEntry> &, void *);
    static PLDHashOperator ClosePersistentConnectionsCB(const nsACString &, nsAutoPtr<nsConnectionEntry> &, void *);
    bool     ProcessPendingQForEntry(nsConnectionEntry *);
    bool     IsUnderPressure(nsConnectionEntry *ent,
                             nsHttpTransaction::Classifier classification);
    bool     AtActiveConnectionLimit(nsConnectionEntry *, PRUint8 caps);
    nsresult TryDispatchTransaction(nsConnectionEntry *ent,
                                    bool onlyReusedConnection,
                                    nsHttpTransaction *trans);
    nsresult DispatchTransaction(nsConnectionEntry *,
                                 nsHttpTransaction *,
                                 nsHttpConnection *);
    nsresult DispatchAbstractTransaction(nsConnectionEntry *,
                                         nsAHttpTransaction *,
                                         PRUint8,
                                         nsHttpConnection *,
                                         PRInt32);
    nsresult BuildPipeline(nsConnectionEntry *,
                           nsAHttpTransaction *,
                           nsHttpPipeline **);
    bool     RestrictConnections(nsConnectionEntry *);
    nsresult ProcessNewTransaction(nsHttpTransaction *);
    nsresult EnsureSocketThreadTargetIfOnline();
    void     ClosePersistentConnections(nsConnectionEntry *ent);
    nsresult CreateTransport(nsConnectionEntry *, nsAHttpTransaction *,
                             PRUint8, bool);
    void     AddActiveConn(nsHttpConnection *, nsConnectionEntry *);
    void     StartedConnect();
    void     RecvdConnect();

    nsConnectionEntry *GetOrCreateConnectionEntry(nsHttpConnectionInfo *);

    bool     MakeNewConnection(nsConnectionEntry *ent,
                               nsHttpTransaction *trans);
    bool     AddToShortestPipeline(nsConnectionEntry *ent,
                                   nsHttpTransaction *trans,
                                   nsHttpTransaction::Classifier classification,
                                   PRUint16 depthLimit);

    // Manage the preferred spdy connection entry for this address
    nsConnectionEntry *GetSpdyPreferredEnt(nsConnectionEntry *aOriginalEntry);
    void               RemoveSpdyPreferredEnt(nsACString &aDottedDecimal);
    nsHttpConnection  *GetSpdyPreferredConn(nsConnectionEntry *ent);
    nsDataHashtable<nsCStringHashKey, nsConnectionEntry *>   mSpdyPreferredHash;
    nsConnectionEntry *LookupConnectionEntry(nsHttpConnectionInfo *ci,
                                             nsHttpConnection *conn,
                                             nsHttpTransaction *trans);

    void               ProcessSpdyPendingQ(nsConnectionEntry *ent);
    void               ProcessAllSpdyPendingQ();
    static PLDHashOperator ProcessSpdyPendingQCB(
        const nsACString &key, nsAutoPtr<nsConnectionEntry> &ent,
        void *closure);

    // message handlers have this signature
    typedef void (nsHttpConnectionMgr:: *nsConnEventHandler)(PRInt32, void *);

    // nsConnEvent
    //
    // subclass of nsRunnable used to marshall events to the socket transport
    // thread.  this class is used to implement PostEvent.
    //
    class nsConnEvent;
    friend class nsConnEvent;
    class nsConnEvent : public nsRunnable
    {
    public:
        nsConnEvent(nsHttpConnectionMgr *mgr,
                    nsConnEventHandler handler,
                    PRInt32 iparam,
                    void *vparam)
            : mMgr(mgr)
            , mHandler(handler)
            , mIParam(iparam)
            , mVParam(vparam)
        {
            NS_ADDREF(mMgr);
        }

        NS_IMETHOD Run()
        {
            (mMgr->*mHandler)(mIParam, mVParam);
            return NS_OK;
        }

    private:
        virtual ~nsConnEvent()
        {
            NS_RELEASE(mMgr);
        }

        nsHttpConnectionMgr *mMgr;
        nsConnEventHandler   mHandler;
        PRInt32              mIParam;
        void                *mVParam;
    };

    nsresult PostEvent(nsConnEventHandler  handler,
                       PRInt32             iparam = 0,
                       void               *vparam = nsnull);

    // message handlers
    void OnMsgShutdown             (PRInt32, void *);
    void OnMsgNewTransaction       (PRInt32, void *);
    void OnMsgReschedTransaction   (PRInt32, void *);
    void OnMsgCancelTransaction    (PRInt32, void *);
    void OnMsgProcessPendingQ      (PRInt32, void *);
    void OnMsgPruneDeadConnections (PRInt32, void *);
    void OnMsgSpeculativeConnect   (PRInt32, void *);
    void OnMsgReclaimConnection    (PRInt32, void *);
    void OnMsgCompleteUpgrade      (PRInt32, void *);
    void OnMsgUpdateParam          (PRInt32, void *);
    void OnMsgClosePersistentConnections (PRInt32, void *);
    void OnMsgProcessFeedback      (PRInt32, void *);

    // Total number of active connections in all of the ConnectionEntry objects
    // that are accessed from mCT connection table.
    PRUint16 mNumActiveConns;
    // Total number of idle connections in all of the ConnectionEntry objects
    // that are accessed from mCT connection table.
    PRUint16 mNumIdleConns;

    // Holds time in seconds for next wake-up to prune dead connections. 
    PRUint64 mTimeOfNextWakeUp;
    // Timer for next pruning of dead connections.
    nsCOMPtr<nsITimer> mTimer;

    // A 1s tick to call nsHttpConnection::ReadTimeoutTick on
    // active http/1 connections. Disabled when there are no
    // active connections.
    nsCOMPtr<nsITimer> mReadTimeoutTick;
    bool mReadTimeoutTickArmed;

    //
    // the connection table
    //
    // this table is indexed by connection key.  each entry is a
    // nsConnectionEntry object.
    //
    nsClassHashtable<nsCStringHashKey, nsConnectionEntry> mCT;

    // mAlternateProtocolHash is used only for spdy/* upgrades for now
    // protected by the monitor
    nsTHashtable<nsCStringHashKey> mAlternateProtocolHash;
    static PLDHashOperator TrimAlternateProtocolHash(nsCStringHashKey *entry,
                                                     void *closure);
    // Read Timeout Tick handlers
    void ActivateTimeoutTick();
    void ReadTimeoutTick();
    static PLDHashOperator ReadTimeoutTickCB(const nsACString &key,
                                             nsAutoPtr<nsConnectionEntry> &ent,
                                             void *closure);

    // For diagnostics
    void OnMsgPrintDiagnostics(PRInt32, void *);
    static PLDHashOperator PrintDiagnosticsCB(const nsACString &key,
                                              nsAutoPtr<nsConnectionEntry> &ent,
                                              void *closure);
    nsCString mLogData;
};

#endif // !nsHttpConnectionMgr_h__
