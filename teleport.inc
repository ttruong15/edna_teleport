<?php

class Teleport {
    private $logDir;
    private $cacheDir;
    private $configDir;
    private $failedDir;
    private $docker;
    private $currentBlockLog;
    private $cleosCommand;
    private $eosCommand;
    private $telosCommand;
    private $telosNodeModifyTimestamp;
    private $eosNodeModifyTimestamp;

    private $failedNodeLog;    // keep a log of all failed nodes
    private $runningLogFile;    // log the running status of the script

    private $eosNodeFile;        // list of eos bp nodes to use
    private $telosNodeFile;      // list of telos pb nodes to use

    private $eosNodes = array();        // list of eos bp nodes to use
    private $telosNodes = array();      // list of telos pb nodes to use

    private $localNode;
    private $telosWalletPasswordFile;
    private $eosWalletPasswordFile;

    private $containerName = null;
    private $ourNode = true;   // whether to use our own local node (set=>true) or other people node (set => false)
    private $currentNode = null;
    private $chainName = null;
    private $cacheFilename = null;
    private $headBlockNum = null;
    private $blockHeight = null;
    private $issueTokenEosFailNum = 0;
    private $issueTokenTelosFailNum = 0;

    const MAX_ISSUE_TOKEN_FAILED_RETRY = 2;

    private $telegram = null;

    public function __construct($chain = 'eos', $telegram=null) {
        $this->chainName = $chain;
        $this->readConfig();
        $this->checkRequirement();
        $this->telegram = $telegram;
    }

    public function checkRequirement() {
        if (!is_dir($this->cacheDir)) {
            throw new Exception("Directory not available: " . $this->cacheDir);
        }
        if (!is_dir($this->cacheDir . $this->chainName)) {
            echo "create " . $this->cacheDir.$this->chainName . " directory\n";;
            mkdir($this->cacheDir.$this->chainName);
        }
        if (!is_file($this->docker)) {
            throw new Exception("Docker is not available: " . $this->docker);
        }
        if (!is_dir($this->failedDir)) {
            echo "create " . $this->failedDir . " directory\n";
            mkdir($this->failedDir);
        }
        if (!is_dir($this->logDir)) {
            echo "create " . $this->logDir . " directory\n";
            mkdir($this->logDir);
        }
        if (!is_dir($this->configDir)) {
            echo "create " . $this->configDir . " directory\n";
            mkdir($this->configDir);
        }

        if(!file_exists($this->telosWalletPasswordFile) || !file_get_contents($this->telosWalletPasswordFile)) {
            throw new Exception("Teleport required wallet password for telos, update teleport.json file 'telos-wallet-password-file': 'location_your_wallet_password_file'");
        }

        if(!file_exists($this->eosWalletPasswordFile) || !file_get_contents($this->eosWalletPasswordFile)) {
            throw new Exception("Teleport required wallet password for eos, update teleport.json file 'eos-wallet-password-file': 'location_your_wallet_password_file'");
        }
    }

    public function setOurNode($ourNode) {
        $this->ourNode = (bool) $ourNode;
    }

    public function notifyTelegram(array $data) {
        if(is_object($this->telegram) && $this->telegram instanceof Telegram)
        {
            try {
                $message = date("jS F H:i:s e") . "\n\n";
                $message .= "<b>Successfully issue token on " . $data['chain'] . "</b>\n";
                if ($data['chain'] === "eos") {
                    $message .= 'https://telostracker.io/trx/' . $data['transactionId'] . "\n\n";
                } else {
                    $message .= 'https://eosflare.io/tx/' . $data['transactionId'] . "\n\n";
                }
                $message .= "<b>From:</b> " . $data['fromAccountName'] . "\n";
                $message .= "<b>To:</b> " . $data['toAccountName'] . "\n";
                $message .= "<b>Quantity:</b> " . $data['quantity'] . "\n";

                if ($data['issueTransactionId']) {
                    if ($data['chain'] === "eos") {
                        $message .= 'https://eosflare.io/tx/' . $data['issueTransactionId'] . "\n\n";
                    } else if ($data['chain'] === "telos") {
                        $message .= 'https://telostracker.io/trx/' . $data['issueTransactionId'] . "\n\n";
                    }
                }
                $this->telegram->sendMe(array('text' => $message));
            } catch (Exception $e) {
                print_r($e);
                // ignore
            }
        }
    }

    public function parseTransactionId($data) {
        $issueTransId = "";
        if (preg_match("/.+transaction: ([^\s]+) .+/", $data, $match)) {
            $issueTransId = $match[1];
        }

        return $issueTransId;
    }

    public function issueTokenOnTelos($toAccountName, $quantity, $fromAccountName, $transactionId) {
        $memo = "teleport from $fromAccountName on EOS - " . substr($transactionId, 0, 8);
        $command = $this->docker . ' exec ' . $this->containerName . ' sh -c "' . $this->telosCommand . ' -u ' . $this->getTelosNode(true) . ' --wallet-url ' . $this->getTelosWalletNode() . ' push action ednazztokens issue \'{\"to\": \"' . trim($toAccountName) . '\", \"quantity\": \"' . $quantity . ' EDNA\", \"memo\": \"' . $memo . '\" }\' -p ednazztokens 2>&1"';

        try {
            $this->checkAndUnlockWallet("telos");
            $result = $this->runCommand($command, false);
            $issueTransId = $this->parseTransactionId($result);

            $this->log('issue token on telos success: ' . $command . " Result: " . $result);
            $this->notifyTelegram(
                    array(
                        'toAccountName' => $toAccountName,
                        'quantity' => $quantity,
                        'fromAccountName' => $fromAccountName,
                        'transactionId' => $transactionId,
                        'chain' => 'telos',
                        'status' => 'Successfully',
                        'issueTransactionId' => $issueTransId
                    )
            );
            $this->issueTokenTelosFailNum = 0;
        } catch (Exception $e) {
            if ($this->issueTokenTelosFailNum > self::MAX_ISSUE_TOKEN_FAILED_RETRY) {
                $this->log('ERROR: issue token telos blockheight: ' . $this->blockHeight . ' - ' . $command);
                $datetime = date("Y-m-d H:i:s");
                file_put_contents($this->issueFailedLogFile, "$datetime - BlockNum: " . $this->blockHeight . " " . $command, FILE_APPEND);
                $this->notifyTelegram(array('toAccountName' => $toAccountName, 'quantity' => $quantity, 'fromAccountName' => $fromAccountName, 'transactionId' => $transactionId, 'chain' => 'telos', 'status' => 'Failed'));
                throw new Exception("Failed issue token");
            } else {
                ++$this->issueTokenTelosFailNum;
                throw $e;
            }
        }
    }

    public function issueTokenOnEOS($toAccountName, $quantity, $fromAccountName, $transactionId) {
        $memo = "teleport from $fromAccountName on TELOS - " . substr($transactionId, 0, 8);
        $command = $this->docker . ' exec ' . $this->containerName . ' sh -c "' . $this->eosCommand . ' -u ' . $this->getEosNode(true) . ' --wallet-url ' . $this->getEosWalletNode() . ' push action ednazztokens issue \'{\"to\": \"' . trim($toAccountName) . '\", \"quantity\": \"' . $quantity . ' EDNA\", \"memo\": \"' . $memo . '\" }\' -p ednazztokens 2>&1"';
        try {
            $this->checkAndUnlockWallet("eos");
            $result = $this->runCommand($command, false);
            $issueTransId = $this->parseTransactionId($result);

            $this->log('issue token on eos success: ' . $command . " Result: " . $result);
            $this->notifyTelegram(
                    array(
                        'toAccountName' => $toAccountName,
                        'quantity' => $quantity,
                        'fromAccountName' => $fromAccountName,
                        'transactionId' => $transactionId,
                        'chain' => 'eos',
                        'status' => 'Successfully',
                        'issueTransactionId' => $issueTransId
                    )
            );
            $this->issueTokenEosFailNum = 0;
        } catch (Exception $e) {
            if ($this->issueTokenEosFailNum > self::MAX_ISSUE_TOKEN_FAILED_RETRY) {
                $this->log('ERROR: issue token eos blockheight: ' . $this->blockHeight . ' - ' . $command);
                $datetime = date("Y-m-d H:i:s");
                file_put_contents($this->issueFailedLogFile, "$datetime - BlockNum: " . $this->blockHeight . " " . $command, FILE_APPEND);
                $this->notifyTelegram(array('toAccountName' => $toAccountName, 'quantity' => $quantity, 'fromAccountName' => $fromAccountName, 'transactionId' => $transactionId, 'chain' => 'eos', 'status' => 'Failed'));
                throw new Exception("Failed issue token");
            } else {
                ++$this->issueTokenEosFailNum;
                throw $e;
            }
        }
    }

    public function checkAndUnlockWallet($chainName) {
        if($chainName === "eos") {
            $command = $this->docker . ' exec ' . $this->containerName . ' sh -c "' . $this->eosCommand . ' --wallet-url ' . $this->getEosWalletNode() . ' wallet list 2>&1"';
        } else {
            $command = $this->docker . ' exec ' . $this->containerName . ' sh -c "' . $this->telosCommand . ' --wallet-url ' . $this->getTelosWalletNode() . ' wallet list 2>&1"';
        }

        $result = $this->runCommand($command, false);
        $resultString = str_replace("\n", "", $result);

        if (preg_match("/default \*/", $resultString)) {
            // wallet available and unlock, continue processing
        } else if (preg_match("/default/", $resultString)) {
            // wallet available but locked, so we need to unlocked it
            if($chainName === "eos") {
                $this->unlockEosWallet();
            } else {
                $this->unlockTelosWallet();
            }
        } else {
            if($chainName === "eos") {
                $this->openEosWallet();
                $this->unlockEosWallet();
            } else {
                $this->openTelosWallet();
                $this->unlockTelosWallet();
            }
        }
    }

    public function getRunningLogFile() {
        return $this->runningLogFile;
    }

    public function getBlockHeight() {
        // Only need to get the headblock num if it's first time run or when blockHeight catchup with the current headblocknum
        if ($this->headBlockNum === null || $this->blockHeight >= $this->headBlockNum) {
            $this->headBlockNum = $this->getLastIrreversibleBlockNum();
        }

        if (file_exists($this->currentBlockLog)) {
            $this->blockHeight = trim(file_get_contents($this->currentBlockLog));
        }

        // if no blockHeight yet, which mean we running for the first time so start from the headblocknum since our
        // contract only available from headblocknum block.
        if (!$this->blockHeight) {
            $this->blockHeight = $this->headBlockNum;
        }
    }

    public function runProcessBlockRange($startBlockNum, $endBlockNum) {
        $cacheFilenameRange = $this->cacheDir . $this->chainName . '/' . $startBlockNum . "-" . $endBlockNum;
        file_put_contents($cacheFilenameRange, "");
        while ($startBlockNum <= $endBlockNum) {
            $this->blockHeight = $startBlockNum;
            echo "T";
            try {
                $this->cacheFilename = $this->cacheDir . $this->chainName . '/' . $startBlockNum;
                $this->log("Processing current block " . $startBlockNum . " out of " . $endBlockNum . " URL: " . $this->currentNode);
                $transactions = $this->getTransactions($startBlockNum);
                if (is_array($transactions) && count($transactions)) {
                    $this->processTransactions($transactions);
                }

                if (file_exists($this->cacheFilename)) {
                    unlink($this->cacheFilename);
                }
                $startBlockNum++;
            } catch (Exception $e) {
                echo "E";
                $this->logNodeErrors($e->getMessage(), $startBlockNum);
            }
        }
        if (file_exists($cacheFilenameRange)) {
            unlink($cacheFilenameRange);
        }
    }

    public function runProcess() {
        while (true) {
            echo "T";
            try {
                $this->getBlockHeight();

                if ($this->blockHeight < $this->headBlockNum) {
                    $this->cacheFilename = $this->cacheDir . $this->chainName . '/' . $this->blockHeight;

                    $this->log("Processing current block " . $this->blockHeight . " out of " . $this->headBlockNum . " URL: " . $this->currentNode);
                    $transactions = $this->getTransactions($this->blockHeight);

                    if (is_array($transactions) && count($transactions)) {
                        $this->processTransactions($transactions);
                    }

                    if (file_exists($this->cacheFilename)) {
                        unlink($this->cacheFilename);
                    }
                } else {
                    sleep(5);
                }

                $this->saveCurrentBlock($this->blockHeight);
            } catch (Exception $e) {
                echo "E";
                $this->logNodeErrors($e->getMessage(), $this->blockHeight);
            }
        }
    }

    private function logNodeErrors($message, $blockHeight) {
        if (preg_match("/is nodeos running/", $message)) {
            $this->log("Node not available - " . $blockHeight . " Message: " . $message);
            $this->logFailedNode();
        } else if (preg_match("/Error 3200004: fail to resolve host/", $message)) {
            $this->log('Host not available - blockheight: ' . $blockHeight . " Message: " . $message);
        } else if (preg_match("/No such container:/", $message)) {
            $this->log('No container available - blockheight: ' . $blockHeight . " Message: " . $message);
        } else if (preg_match("/Failed with error: Parse Error/", $message)) {
            $this->log('Temporary parse error - blockheight: ' . $blockHeight . " Message: " . $message);
        } else {
            $this->log('UNKNOW ERROR - blockheight: ' . $blockHeight . " Message: " . $message);
        }
    }

    private function logFailedActions($transactionId, $blockHeight, $message) {
        if (preg_match("/is nodeos running/", $message)) {
            $this->log("Node not available - " . $blockHeight . " Message: " . $message);
        } else if (preg_match("/is keosd running/", $message)) {
            $this->log("Wallet keosd not running - " . $blockHeight . " Message: " . $message);
        } else if (preg_match("/Failed with error: Parse Error/", $message)) {
            $this->log('Issue token failed - parse error: ' . $transactionId . ' blockheight: ' . $blockHeight);
        } else if (preg_match("/Error 3050003: eosio_assert_message assertion failure/", $message)) {
            $this->log('Issue token failed - transaction failed: ' . $transactionId . ' blockheight: ' . $blockHeight);
        } else if (preg_match("/Error 3040008: Duplicate transaction/", $message)) {
            $this->log('Issue token failed - duplicate transaction: ' . $transactionId . ' blockheight: ' . $blockHeight);
        } else if (preg_match("/Error 3200004: fail to resolve host/", $message)) {
            $this->log('Host not available - blockheight: ' . $blockHeight . " Message: " . $message);
        } else if (preg_match("/Error 3120006: No available wallet/", $message)) {
            // wallet have not been open yet
            $this->log('Wallet not available - blockheight: ' . $blockHeight . " Message: " . $message);
        } else if (preg_match("/Error 3120003: Locked wallet/", $message)) {
            $this->log('Locked wallet - blockheight: ' . $blockHeight . " Message: " . $message);
        } else if (preg_match("/Error 3120007: Already unlocked/", $message)) {
            $this->log('Wallet already unlock - blockheight: ' . $blockHeight . " Message: " . $message);
        } else {
            $this->log('Issue token failed - UNKNOWN: ' . $transactionId . ' blockheight: ' . $blockHeight . ' ' . $message);
        }
    }

    public function logFailedNode() {
        $failedNodes = array();
        if (file_exists($this->failedNodeLog)) {
            $failedNodes = json_decode(file_get_contents($this->failedNodeLog), true);
            if(array_key_exists($this->currentNode, $failedNodes)) {
                ++$failedNodes[$this->currentNode];
            } else {
                $failedNodes[$this->currentNode] = 1;
            }
        } else {
            $failedNodes[$this->currentNode] = 1;
        }
        file_put_contents($this->failedNodeLog, json_encode($failedNodes));
    }

    public function processTransactions($transactions) {
        foreach ($transactions as $transaction) {
            if ($transaction['status'] === "executed") {
                if (is_array($transaction['trx'])) {
                    if (array_key_exists('transaction', $transaction['trx'])) {
                        $transactionId = $transaction['trx']['id'];
                        //$this->log("transaction: " . $transaction['trx']['id']);
                        $failed = false;
                        foreach ($transaction['trx']['transaction']['actions'] as $action) {
                            $cacheTransaction = array();
                            if (file_exists($this->cacheFilename)) {
                                $cacheTransaction = json_decode(file_get_contents($this->cacheFilename), true);
                            }

                            // transaction have already actioned.
                            if (is_array($cacheTransaction) && array_key_exists($transactionId, $cacheTransaction)) {
                                continue;
                            }
                            do {
                                echo ".";
                                $failedTransactionFilename = $this->failedDir . $transaction['trx']['id'] . '-' . $this->blockHeight;
                                try {
                                    $this->processAction($action, $transactionId);

                                    // if transaction successfully we cached it, so if something happen we dont want to process this transaction again.
                                    $cacheTransaction[$transactionId] = $action;
                                    file_put_contents($this->cacheFilename, json_encode($cacheTransaction));

                                    $failed = false;
                                    if(file_exists($failedTransactionFilename)) {
                                        unlink($failedTransactionFilename);
                                    }
                                } catch (Exception $e) {
                                    $failed = true;
                                    file_put_contents($failedTransactionFilename, json_encode($action));
                                    if (preg_match("/Failed issue token/", $e->getMessage())) {
                                        // Unable to issue token for whatever reason, allow to continue on the next block.
                                        $failed = false;
                                    }

                                    $this->logFailedActions($transactionId, $this->blockHeight, $e->getMessage());
                                }
                            } while ($failed);
                        }
                    }
                }
            }
        }
    }

    public function processAction($action, $transactionId) {
        if ($action['account'] === "ednazztokens" && in_array($action['name'], array("teleporttlos", "teleporteos"))) {
            list($quantity, ) = explode(" ", $action['data']['quantity']);

            if ($this->chainName === "eos") {
                $toAcountName = $action['data']['tlos_address_to'];
                $this->issueTokenOnTelos($toAcountName, $quantity, $action['data']['from'], $transactionId);
            } else {
                $toAcountName = $action['data']['eos_address_to'];
                $this->issueTokenOnEOS($toAcountName, $quantity, $action['data']['from'], $transactionId);
            }
        }
    }

    public function saveCurrentBlock($currentBlock) {
        // store the next block num, we need to keep track of which block we up to
        // in case we have to start the process again.

        file_put_contents($this->currentBlockLog, ++$currentBlock);
    }

    public function getCurrentBlockHeight() {
        $currentBlockHeight = 0;
        if (file_exists($this->currentBlockLog)) {
            $currentBlockHeight = trim(file_get_contents($this->currentBlockLog));
        }

        return $currentBlockHeight;
    }

    public function getLastIrreversibleBlockNum() {
        try {
            $chainInfo = $this->getChainInfo();
            if (is_array($chainInfo)) {
                return $chainInfo['last_irreversible_block_num'];
            }
        } catch (Exception $e) {
            throw $e;
        }
    }

    public function getHeadBlockNum() {
        try {
            $chainInfo = $this->getChainInfo();

            if (is_array($chainInfo)) {
                return $chainInfo['head_block_num'];
            }
        } catch (Exception $e) {
            throw $e;
        }
    }

    public function getTransactions($blockNum) {
        try {
            $block = $this->getBlock($blockNum);

            return $block['transactions'];
        } catch (Exception $e) {
            throw $e;
        }
    }

    public function getBlock($blockNum) {
        $command = $this->docker . ' exec ' . $this->containerName . ' sh -c "' . $this->cleosCommand . ' -u ' . $this->getNodeURL() . ' get block ' . $blockNum . ' 2>&1"';

        try {
            return $this->runCommand($command, true);
        } catch (Exception $e) {
            throw $e;
        }
    }

    public function getEosWalletNode() {
        return $this->eosWalletNode;
    }

    public function getTelosWalletNode() {
        return $this->telosWalletNode;
    }

    public function getChainInfo() {
        $command = $this->docker . ' exec ' . $this->containerName . ' sh -c "' . $this->cleosCommand . ' -u ' . $this->getNodeURL() . ' get info 2>&1"';

        try {
            return $this->runCommand($command);
        } catch (Exception $e) {
            throw $e;
        }
    }

    public function getNodeURL($alwaysUseExternal = false) {
        if ($this->ourNode && !$alwaysUseExternal) {
            $this->currentNode = $this->localNode;
        } else {
            $this->currentNode = $this->getRandomNode($this->chainName);
        }

        return $this->currentNode;
    }

    public function getTelosNode($alwaysUseExternal = false) {
        if ($this->ourNode && !$alwaysUseExternal) {
            $this->currentNode = $this->localNode;
        } else {
            $this->currentNode = $this->getRandomNode('telos');
        }

        return $this->currentNode;
    }

    public function getEosNode($alwaysUseExternal = false) {
        if ($this->ourNode && !$alwaysUseExternal) {
            $this->currentNode = $this->localNode;
        } else {
            $this->currentNode = $this->getRandomNode('eos');
        }

        return $this->currentNode;
    }

    public function openEosWallet() {
        $command = $this->docker . ' exec ' . $this->containerName . ' sh -c "' . $this->eosCommand . ' -u ' . $this->getEosNode() . ' --wallet-url ' . $this->getEosWalletNode() . ' wallet open 2>&1"';

        $result = $this->runCommand($command, false);
        if (substr($result, 0, 6) !== "Opened") {
            throw new Exception("Unable to open wallet: " . $result);
        }
    }

    public function openTelosWallet() {
        $command = $this->docker . ' exec ' . $this->containerName . ' sh -c "' . $this->cleosCommand . ' -u ' . $this->getTelosNode() . ' --wallet-url ' . $this->getTelosWalletNode() . ' wallet open 2>&1"';

        $result = $this->runCommand($command, false);
        if (substr($result, 0, 6) !== "Opened") {
            throw new Exception("Unable to open wallet: " . $result);
        }
    }

    public function unlockEosWallet() {
        $password = $this->getWalletPassword("eos");
        $command = $this->docker . ' exec ' . $this->containerName . ' sh -c "' . $this->eosCommand . ' --wallet-url ' . $this->getEosWalletNode() . ' wallet unlock --password ' . $password . ' 2>&1"';

        $result = $this->runCommand($command, false);
        if (substr($result, 0, 8) !== "Unlocked") {
            throw new Exception("Unable to unlock wallet: " . $result);
        }
    }

    public function unlockTelosWallet() {
        $password = $this->getWalletPassword('telos');
        $command = $this->docker . ' exec ' . $this->containerName . ' sh -c "' . $this->telosCommand . ' --wallet-url ' . $this->getTelosWalletNode() . ' wallet unlock --password ' . $password . ' 2>&1"';

        $result = $this->runCommand($command, false);
        if (substr($result, 0, 8) !== "Unlocked") {
            throw new Exception("Unable to unlock wallet: " . $result);
        }
    }

    private function getWalletPassword($chainName) {
        if($chainName === "eos") {
            return file_get_contents($this->eosWalletPasswordFile);
        } else {
            return file_get_contents($this->telosWalletPasswordFile);
        }
    }

    /**
     *   run docker container command
     *
     *   $arrayOnly - set to true to expect successfully response to have a valid json.
     *
     *   @params $command string
     *   @params $arrayOnly boolean
     *
     *   @return mixed
     */
    private function runCommand($command, $arrayOnly = true) {
        ob_start();
        @system($command, $retval);
        $resultString = ob_get_contents();
        ob_end_clean();

        $resultArray = json_decode(mb_convert_encoding($resultString, 'UTF-8', 'UTF-8'), true);
        if (json_last_error()) {
            if (preg_match("/Error/i", $resultString) || preg_match("/Failed to connect to nodeos/", $resultString)) {
                throw new Exception($resultString);
            } else {
                if (!$arrayOnly) {
                    return $resultString;
                }
                throw new Exception($resultString);
            }
        }

        return $resultArray;
    }

    private function log($message) {
        $logMessage = date("Y-m-d H:i:s", time()) . " - " . $message . "\n";

        if (file_exists($this->runningLogFile)) {
            $fileSize = filesize($this->runningLogFile) / 1024 / 1024;
            // Size in Megabyte, if bigger then 100M compress and rotate
            if ($fileSize > 100) {
                $compressContent = gzcompress(file_get_contents($this->runningLogFile));
                file_put_contents($this->runningLogFile . date("YmdHis").".gz", $compressContent);
                file_put_contents($this->runningLogFile, "");
            }
        }
        file_put_contents($this->runningLogFile, $logMessage, FILE_APPEND);
    }

    /**
     * 	randomly grab a single url from the list.
     *
     */
    private function getRandomNode($chainName) {
        list($usec, $sec) = explode(' ', microtime());
        srand($sec + $usec * 1000000);
        if($chainName == "eos") {
            $nodes = $this->getEosNodes();
        } else if($chainName == "telos") {
            $nodes = $this->getTelosNodes();
        }

        $numNode = count($nodes) - 1;
        $randIdx = rand(0, $numNode);

        return $nodes[$randIdx];
    }

    /**
     * 	get a list of available telos nodes endpoint.
     *
     */
    private function getEosNodes() {
        if(is_array($this->eosNodes) && count($this->eosNodes) && $this->eosNodeModifyTimestamp == filemtime($this->eosNodeFile)) {
            return $this->eosNodes;
        }

        $this->eosNodeModifyTimestamp = filemtime($this->eosNodeFile);
        $fp = fopen($this->eosNodeFile, "r");
        if (!$fp) {
            throw new Exception("Unable to open config file");
        }

        $this->eosNodes = array();
        while (($line = fgets($fp, 1024)) !== false) {
            $this->eosNodes[] = trim($line);
        }
        fclose($fp);
        return $this->eosNodes;
    }

    /**
     * 	get a list of available nodes endpoint.
     *
     */
    private function getTelosNodes() {
        if(is_array($this->telosNodes) && count($this->telosNodes) && $this->telosNodeModifyTimestamp == filemtime($this->telosNodeFile)) {
            return $this->telosNodes;
        }

        $this->telosNodeModifyTimestamp = filemtime($this->telosNodeFile);

        $fp = fopen($this->telosNodeFile, "r");
        if (!$fp) {
            throw new Exception("Unable to open config file");
        }

        $this->telosNodes = array();
        while (($line = fgets($fp, 1024)) !== false) {
            $this->telosNodes[] = trim($line);
        }
        fclose($fp);
        return $this->telosNodes;
    }

    private function readConfig() {
        if(!file_exists("teleport.json")) {
            throw new Exception("teleport.json is not available");
        }

        $config = json_decode(file_get_contents("teleport.json"), true);
        if(json_last_error()) {
            throw new Exception("ERROR: reading teleport.json - " . json_last_error_msg());
        }

        if(empty($this->chainName)) {
            throw new Exception("ERROR: chain name is required.");
        }

        $this->containerName = $config['docker-container-name'];
        if(empty($this->containerName)) {
            throw new Exception("You must specify the docker container name");
        }

        $this->logDir = $config['log-dir'];
        $this->cacheDir = $config['cache-dir'];
        $this->configDir = $config['config-dir'];
        $this->failedDir = $config['failed-dir'];
        $this->failedNodeLog = $this->failedDir . $this->chainName . "_node_failed.log";
        $this->runningLogFile = $this->logDir . "running_" . $this->chainName . '_node.log';
        $this->issueFailedLogFile = $this->logDir . $this->chainName . '_issue_token_failed.log';

        $this->eosNodeFile = $config['eos-node-list'];
        $this->telosNodeFile = $config['telos-node-list'];

        if($this->chainName === "eos") {
            $this->cleosCommand = $config['cleos-command'];
        } else if($this->chainName === "telos") {
            $this->cleosCommand = $config['teclos-command'];
        }

        $this->eosCommand = $config['cleos-command'];
        $this->telosCommand = $config['teclos-command'];

        $this->docker = $config['docker'];
        $this->currentBlockLog = $this->logDir.$this->chainName.'_current_blockheight.log';

        $localNodeName = 'local-'.$this->chainName.'-node';
        $this->localNode = $config[$localNodeName];

        $walletName = $this->chainName.'-wallet-node';
        $this->WalletNode = $config[$walletName];

        $this->eosWalletNode = $config["eos-wallet-node"];
        $this->telosWalletNode = $config["telos-wallet-node"];

        $this->telosWalletPasswordFile = $config["telos-wallet-password-file"];
        $this->eosWalletPasswordFile = $config["eos-wallet-password-file"];
    }
}
