#!/usr/bin/php
<?php
require_once("teleport.inc");

if(count($argv) != 6) {
    echo "Usage: php {$argv[0]} <chain_name(eos|telos)> <use_ournode(0/1)> <turn_on_telegram_bot(0/1) <startBlockNum> <endBlockNum>\n";
    echo "\n";
    echo "Example:\n";
    echo "\t Running EOS chain using our node, turn on telegram and start from block number 10000 to 10100\n";
    echo "php {$argv[0]} eos 1 1 10000 10100\n";
    echo "\n";
    exit;
}

$chainName = isset($argv[1]) ? $argv[1] : "eos";
$useOurOwnNodeOnly = $argv[2];
$enableTelegramBot = $argv[3];
$startBlock = $argv[4];
$endBlock = $argv[5];

try {
    $telegram = null;
    if($enableTelegramBot) {
        $telegram = new Telegram();
    }

    $teleport = new Teleport($chainName, $telegram);
    $teleport->setOurNode($useOurOwnNodeOnly);
    $teleport->runProcessBlockRange($startBlock, $endBlock);
} catch(Exception $e) {
    echo date("Y-m-d H:i:s") . " - Error: " . $e->getMessage() . "\n\n";
	print_r($e);
}
