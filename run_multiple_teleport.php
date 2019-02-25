<?php
require_once("teleport.inc");
require_once("telegram.inc");

if(count($argv) < 2) {
    echo "Usage: php {$argv[0]} <chain_name(eos|telos)> <use_our_node(0/1)> <turn_on_telegram_bot(0/1)\n";
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
$useOurOwnNodeOnly = isset($argv[2]) ? $argv[2] : 0;
$enableTelegramBot = isset($argv[3]) ? $argv[3] : 0;

$maxSpawn = 8;  // Max number of process allow to spawn
$maxBlockGap = 100;
$numSpawn = array();
$logFilename = $chainName."_spawn_teleport.log";

try {
    $telegram = null;
    if($enableTelegramBot) {
        $telegram = new Telegram();
    }
    $teleport = new Teleport($chainName, $telegram);
    $teleport->setOurNode($useOurOwnNodeOnly);
    while(true) {
        if(file_exists("./exit")) {
            unlink("exit");
            exit;
        }
        echo ".";
        // allow max number process to spawn
        try {
            if(count($numSpawn) <= $maxSpawn) {
                $currentHeadBlockHeight = $teleport->getLastIrreversibleBlockNum();
                $currentBlockHeight = $teleport->getcurrentBlockHeight();
                if(!$currentBlockHeight) {
                    $currentBlockHeight = $currentHeadBlockHeight;
                    $teleport->saveCurrentBlock($currentBlockHeight);
                }

                if($currentBlockHeight < $currentHeadBlockHeight) {
                    $currentGap = $currentHeadBlockHeight - $currentBlockHeight;
                    $startBlock = $currentBlockHeight;
                    $endBlock = $currentHeadBlockHeight;

                    if($currentGap > $maxBlockGap) {
                        $blockPerProcess = intval($maxBlockGap/$maxSpawn);
                        for($i=1; $i<=$maxSpawn; $i++) {
                            if($i === $maxSpawn && $startBlock > $currentHeadBlockHeight) {
                                $startBlock += ($blockPerProcess + 1);
                                $endBlock = $currentHeadBlockHeight;
                            } else if($i === 1) {
                                $startBlock = $currentBlockHeight;
                                $endBlock = $startBlock + $blockPerProcess;
                            } else {
                                $startBlock += ($blockPerProcess + 1);
                                $endBlock += $blockPerProcess + 1;
                            }

                            $command = "/usr/bin/nohup ~/teleport/spawn_process.php $chainName $useOurOwnNodeOnly $enableTelegramBot $startBlock $endBlock >> $logFilename 2>&1 & echo $!";
                            echo $command . "\n";
                            exec($command, $numSpawn);
                        }
                    } else {
                        $command = "/usr/bin/nohup ~/teleport/spawn_process.php $chainName $useOurOwnNodeOnly $enableTelegramBot $startBlock $endBlock >> $logFilename 2>&1 & echo $!";
                        echo $command . "\n";
                        exec($command, $numSpawn);
                    }
                    $currentBlockHeight = $endBlock;
                    $teleport->saveCurrentBlock($endBlock);
                } else {
                    echo "+";
                    sleep(1);
                }
            } else {
                sleep(1);
            }
            if(is_array($numSpawn) && count($numSpawn)) {
                $numSpawn = checkRunningProcess($numSpawn);
            }
        } catch(Exception $e) {
            echo "Error: " . $e->getMessage() . "\n";
        }
    }
} catch(Exception $e) {
    echo date("Y-m-d H:i:s") . " - Error: " . $e->getMessage() . "\n\n";
}


function checkRunningProcess($numSpawn) {
    $currentRunningProcess = array();
    foreach($numSpawn as $pid) {
        $dirExist = is_dir("/proc/$pid/fd") && file_exists("/proc/$pid/fd");
        if($dirExist) {
            $currentRunningProcess[] = $pid;
        }
    }

    echo "Current running pid: ";
    foreach($currentRunningProcess as $runningPid) {
        echo $runningPid . " ";
    }
    echo "\n";
    return $currentRunningProcess;
}
