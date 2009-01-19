/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/**
 * Protocol tests for SMTP.
 *
 * This test verifies:
 * - Sending a message to an SMTP server (which is also covered elsewhere).
 * - Correct reception of the message by the SMTP server.
 * - Correct saving of the message to the sent folder.
 *
 * Originally written to test bug 429891 where saving to the sent folder was
 * mangling the message.
 */
var type = null;
var test = null;
var server;
var sentFolder;
var transaction;
var originalData;
var finished = false;
var identity = null;
var testFile = do_get_file("../mailnews/compose/test/unit/data/429891_testcase.eml");

const kSender = "from@invalid.com";
const kTo = "invalid@invalid.com";

var msgSendLater = Cc["@mozilla.org/messengercompose/sendlater;1"]
  .getService(Ci.nsIMsgSendLater);

// This listener handles the post-sending of the actual message and checks the
// sequence and ensures the data is correct.
function msll() {
}

msll.prototype = {
  _initialTotal: 0,

  // nsIMsgSendLaterListener
  onStartSending: function (aTotal) {
    this._initialTotal = 1;
    do_check_eq(msgSendLater.sendingMessages, true);
  },
  onProgress: function (aCurrentMessage, aTotal) {
    // XXX Enable this function
  },
  onStatus: function (aMsg) {
    // XXX Do we really need this?
  },
  onStopSending: function (aStatus, aMsg, aTotal, aSuccessful) {
    do_test_finished();
    print("msll onStopSending\n");
    try {
      do_check_eq(aStatus, 0);
      do_check_eq(aTotal, 1);
      do_check_eq(this._initialTotal, 1);
      do_check_eq(msgSendLater.sendingMessages, false);

      do_check_transaction(transaction,
                           ["EHLO test",
                            "MAIL FROM:<" + kSender + "> SIZE=" + originalData.length,
                            "RCPT TO:<" + kTo + ">",
                            "DATA"]);

      // Compare data file to what the server received
      do_check_eq(originalData, server._handler.post);

      // Now wait till the copy is finished for the sent message
      do_test_pending();
    } catch (e) {
      do_throw(e);
    } finally {
      server.stop();

      var thread = gThreadManager.currentThread;
      while (thread.hasPendingEvents())
        thread.processNextEvent(true);
    }
  }
};

// This listener is used to find out when the copying of the message to the
// unsent message folder is completed, and hence can fire off the actual
// sending of the message.
function copyListener() {
}

copyListener.prototype = {
  // nsIMsgSendListener
  onStartSending: function (aMsgID, aMsgSize) {
  },
  onProgress: function (aMsgID, aProgress, aProgressMax) {
  },
  onStatus: function (aMsgID, aMsg) {
  },
  onStopSending: function (aMsgID, aStatus, aMsg, aReturnFile) {
  },
  onGetDraftFolderURI: function (aFolderURI) {
  },
  onSendNotPerformed: function (aMsgID, aStatus) {
  },

  // nsIMsgCopyServiceListener
  OnStartCopy: function () {
  },
  OnProgress: function (aProgress, aProgressMax) {
  },
  SetMessageKey: function (aKey) {
  },
  GetMessageId: function (aMessageId) {
  },
  OnStopCopy: function (aStatus) {
    do_test_finished();

    try {
      do_check_eq(aStatus, 0);

      // Check this is false before we start sending
      do_check_eq(msgSendLater.sendingMessages, false);

      let folder = msgSendLater.getUnsentMessagesFolder(identity);

      // Check we have a message in the unsent message folder
      do_check_eq(folder.getTotalMessages(false), 1);

      // Now do a comparison of what is in the sent mail folder
      var fileData = loadFileToString(folder.filePath);

      // Skip the headers etc that mailnews adds
      var pos = fileData.indexOf("From:");
      do_check_neq(pos, -1);

      fileData = fileData.substr(pos);

      // Check the data is matching.
      do_check_eq(originalData, fileData);

      do_test_pending();
      do_timeout(sendMessageLater(), 0);
    } catch (e) {
      do_throw(e);
    } finally {
      server.stop();

      var thread = gThreadManager.currentThread;
      while (thread.hasPendingEvents())
        thread.processNextEvent(true);

      finished = true;
    }
  },

  // QueryInterface
  QueryInterface: function (iid) {
    if (iid.equals(Ci.nsIMsgSendListener) ||
        iid.equals(Ci.nsIMsgCopyServiceListener) ||
        iid.equals(Ci.nsISupports))
      return this;

    throw Components.results.NS_ERROR_NO_INTERFACE;
  }
};

// This function does the actual send later
function sendMessageLater()
{
  do_test_finished();

  // Set up the SMTP server.
  server = setupServerDaemon();

  type = "sendMessageLater";

  // Handle the server in a try/catch/finally loop so that we always will stop
  // the server if something fails.
  try {
    // Start the fake SMTP server
    server.start(SMTP_PORT);

    // A test to check that we are sending files correctly, including checking
    // what the server receives and what we output.
    test = "sendMessageLater";

    var messageListener = new msll();

    msgSendLater.addListener(messageListener);

    // Send the unsent message
    msgSendLater.sendUnsentMessages(identity);

    server.performTest();

    transaction = server.playTransaction();

    do_timeout(10000, "if (!finished) do_throw('Notifications of message send/copy not received');");

    do_test_pending();

  } catch (e) {
    do_throw(e);
  } finally {
    server.stop();

    var thread = gThreadManager.currentThread;
    while (thread.hasPendingEvents())
      thread.processNextEvent(true);
  }
}

function run_test() {
  // Test file - for bug 429891
  originalData = loadFileToString(testFile);

  Components.classes["@mozilla.org/preferences-service;1"]
            .getService(Components.interfaces.nsIPrefBranch)
    .setBoolPref("mail.really_delete_draft", false);

  // Ensure we have a local mail account, an normal account and appropriate
  // servers and identities.
  loadLocalMailAccount();

  var acctMgr = Cc["@mozilla.org/messenger/account-manager;1"]
                  .getService(Ci.nsIMsgAccountManager);
  acctMgr.setSpecialFolders();

  var account = acctMgr.createAccount();
  incomingServer = acctMgr.createIncomingServer("test", "localhost", "pop3");

  var smtpServer = getBasicSmtpServer();
  identity = getSmtpIdentity(kSender, smtpServer);

  account.addIdentity(identity);
  account.defaultIdentity = identity;
  account.incomingServer = incomingServer;

  sentFolder = gLocalIncomingServer.rootMsgFolder.addSubfolder("Sent");

  do_check_eq(identity.doFcc, true);

  // Now prepare to actually "send" the message later, i.e. dump it in the
  // unsent messages folder.

  var compFields = Cc["@mozilla.org/messengercompose/composefields;1"]
                     .createInstance(Ci.nsIMsgCompFields);

  compFields.from = identity.email;
  compFields.to = kTo;

  var cl = new copyListener(true);

  var msgSend = Cc["@mozilla.org/messengercompose/send;1"]
                  .createInstance(Ci.nsIMsgSend);

  msgSend.sendMessageFile(identity, "", compFields, testFile,
                          false, false, Ci.nsIMsgSend.nsMsgQueueForLater,
                          null, cl, null, null);

  // Now we wait till we get copy notification of completion.
  do_test_pending();
}
