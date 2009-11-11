<?php
function gsconfBatch($commands, $passphrase = '', $colors = false)
{
    $fdSpec = array(
        0 => array('pipe', 'r'), // stdin
        1 => array('pipe', 'w'), // stdout
        2 => array('pipe', 'w')  // stderr
    );

    if(!is_array($commands))
        $commands = array($commands);

    $args = '';
    foreach($commands as $command)
        $args .= ($args ? ' -b ' : '-b ') . escapeshellarg($command);

    $process = proc_open('./gsconf'.($colors ? '' : ' -c').($passphrase ? ' -s' : '').' '.$args, $fdSpec, $pipes);
    if(!$process)
        return false;

    if($passphrase)
        fwrite($pipes[0], $passphrase);
    fclose($pipes[0]);

    echo stream_get_contents($pipes[1]);
    echo stream_get_contents($pipes[2]);
    fclose($pipes[1]);
    fclose($pipes[2]);

    proc_close($process);
    return true;
}


gsconfBatch(array("exec Local.Test 'ls -al'", 'commit --check-remote'), null, true);
?>
