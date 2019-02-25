<?php
require_once("teleport.inc");

if(count($argv) < 2) {
    echo "Usage: php {$argv[0]} <chain_name(eos|telos)> <use_ournode(0/1)> <turn_on_telegram_bot(0/1)\n";
    echo "\n";
    echo "Example:\n";
    echo "\t Running EOS chain using our node\n";
    echo "\t php {$argv[0]} eos 1\n";
    echo "\n";
    echo "\t Running EOS chain using others node\n";
    echo "\t php {$argv[0]} eos 0\n";
    echo "\n";
    echo "\t Running EOS chain using our node and turn on telegram bot\n";
    echo "\t php {$argv[0]} eos 1 1\n";
    echo "\n";
    exit;
}

$chainName = $argv[1];
$useOurOwnNodeOnly = isset($argv[2]) ? $argv[2] : 1;
$enableTelegramBot = isset($argv[3]) ? $argv[3] : 0;

try {
    $telegram = null;
    if($enableTelegramBot) {
        $telegram = new Telegram();
    }
    $teleport = new Teleport($chainName, $telegram);
    $teleport->setOurNode($useOurOwnNodeOnly);
    echo "\n";
    echo "Check the '" . $teleport->getRunningLogFile() . " file to make sure the script is running properly.\n\n";
    $teleport->runProcess();
} catch(Exception $e) {
    echo date("Y-m-d H:i:s") . " - Error: " . $e->getMessage() . "\n\n";
}