<?php

/**
 * @file
 *
 * A collection of Drush commands to make managing islandora a bit easier.
 *
 * Seems like you can't pipe
 */

/**
 * Implements hook_drush_command.
 */
function islandora_drush_command() {
  return array(
    'islandora-purge' => array(
      'callback' => 'drush_islandora_purge',
      'arguments' => array(
        'pid' => 'The PID of the object to purge.',
      ),
      'options' => array(
        'recursive' => array(
          'description' => 'Purge all child and descendant objects as well.',
        ),
        'interactive' => array(
          'description' => 'Prompt the user before deleting every object.'
        )
      ),
      'description' => 'Purges the object identified by the provided PID',
      'examples' => array(
        'drush islandora-purge PID' => 'Delete the object identified by PID',
        'drush islandora-purge --recursive PID' => 'Delete the object identified by PID as well as all descendant elements defined by isMemberOf or isMemberOfCollection relationships',
        'drush islandora-purge --interactive PID' => 'Prompt the user before deleting the object.'
      ),
      'aliases' => array('purge'),
      'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_LOGIN, // We can pass in users id on the command line using drush -u.
    ),
    // @todo Implement
    'islandora-set-state' => array(
      'callback' => 'drush_islandora_set_state',
      'description' => 'Changes the given state of the object identified by the provided PID. The options are "A", "I", "D"',
      'examples' => array(
        'drush islandora-set-state PID STATE',
      ),
      'aliases' => array('set-state'),
      'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_LOGIN, // We can pass in users id on the command line using drush -u.
    ),
    'islandora-cp-datastream' => array(
      'callback' => 'drush_islandora_copy_datastream',
      'description' => 'Copies the given datastream of the given object into another datastream if it is not present.',
      'examples' => array(
        'drush islandora-cp-datastream PID STATE',
      ),
      'aliases' => array('cp-datastream'),
      'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_LOGIN, // We can pass in users id on the command line using drush -u.
    ),
    'islandora-ri-search' => array(
      'callback' => 'drush_islandora_ri_search',
      'description' => 'Returns a list of results of the search.',
      'arguments' => array(
        'query' => 'The itql/sparql query to execute required if --file is not provided',
      ),
      'options' => array(
        'file' => 'A file containing the itql/sparql query to execute, takes precedent over query argument',
        'lang' => 'Interpert the query as either itql/sparql (Defaults to itql)',
        'limit' => 'Limit the number of results',
        'offset' => 'Return results after the given offset',
      ),
      'examples' => array(
        'drush islandora-ri-search \'select $o from <#ri> where $o <fedora-model:state> <fedora-model:Active>\'',
      ),
      'aliases' => array('risearch'),
      'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_LOGIN, // We can pass in users id on the command line using drush -u.
    )
  );
}

/**
 * Purges the object identified by the provided PID
 *
 * @param string $pid
 */
function drush_islandora_purge($pid) {
  static $purged = array();
  $repo = islandora_drush_repository();
  if (drush_get_option('recursive')) {
    $query = 'select $o from <#ri> where ($o <fedora-rels-ext:isMemberOf> <info:fedora/' . $pid . '> or $o <fedora-rels-ext:isMemberOfCollection> <info:fedora/' . $pid . '>)';
    $results = $repo->ri->itqlQuery($query); // Immediate children
    $results = array_map(function($o) { return $o['o']['value']; }, $results); // PIDs only.
    $results = array_diff($results, $purged); // Only call the function for objects not marked as purged.
    $purged = array_merge($purged, $results, array($pid)); // Prevent circular dependencies and infinite recursion.
    array_walk($results, 'drush_islandora_purge');
  }
  try {
    $label = $repo->getObject($pid)->label;
    $repo->purgeObject($pid);
  } catch (Exception $e) {
    drush_set_error(dt('Failed to purge "!PID": !msg', array('!PID' => $pid, '!msg' => $e->getMessage())));
    return;
  }
  drush_print(dt('Successfully purge: "!Label" (!PID)', array('!PID' => $pid, '!Label' => $label)));
}

/**
 * Validate the arguments provided to this drush command.
 */
function drush_islandora_purge_validate() {
  return islandora_drush_include_tuque(); // @todo add additional checks.
}

/**
 * Copies the given object's datastream $src into the $dest if it is not present.
 *
 * @param string $src
 *   The source datastream's DSID to copy from.
 * @param string $dest
 *   The destination datastream's DSID to copy to.
 * @param string $pid
 *   The objects PID.
 */
function drush_islandora_copy_datastream($src, $dest, $pid = NULL) {
  $repo = islandora_drush_repository();
  if (empty($pid)) {
    stream_set_blocking(STDIN, FALSE);
    do {
      $pid = fgets(STDIN);
      $pid = trim($pid);
      if ($pid === FALSE || empty($pid)) {
        break;
      }
      drush_islandora_copy_datastream($src, $dest, $pid);
    } while (!feof(STDIN));
    return TRUE;
  }
  try {
    $obj = $repo->getObject($pid);
    $label = $obj->label;
    if (empty($obj[$src])) {
      throw new Exception('Source datastream does not exist.');
    }
    else {
      if (isset($obj[$dest])) {
        throw new Exception('Destination datastream already exists.');
      }
      else {
        $src_ds = $obj[$src];
        $dest_ds = new NewFedoraDatastream($dest, $src_ds->controlGroup, $obj, $repo);
        $properties = array('logMessage', 'checksumType', 'checksum', 'format', 'mimetype', 'versionable', 'label', 'state');
        $external = array_search($src_ds->controlGroup, array('R', 'E')) !== FALSE;
        $properties[] = $external ? 'url' : 'content';
        $copy_property = function($property) use(&$src_ds, &$dest_ds) { $dest_ds->$property = $src_ds->$property; };
        array_walk($properties, $copy_property);
        if ($obj->ingestDatastream($dest_ds) == FALSE) {
          throw new Exception('Failed to ingest datastream.');
        }
      }
    }
  } catch (Exception $e) {
    return drush_set_error(dt('Failed to copy datastream !SRC to !DEST (!PID): !msg', array('!PID' => $pid, '!SRC' => $src, '!DEST' => $dest, '!msg' => $e->getMessage())));
  }
  drush_print(dt('Successfully copied !SRC to !DEST "!Label" (!PID)', array('!PID' => $pid, '!SRC' => $src, '!DEST' => $dest, '!Label' => $label)));
  return TRUE;
}

/**
 * Validate the arguments provided to this drush command.
 */
function drush_islandora_copy_datastream_validate() {
  return islandora_drush_include_tuque(); // @todo add additional checks.
}

/**
 * Executes a risearch query returning a comma delimited values for each result.
 *
 * @param string $query
 *   Either a itql or sparql query to execute.
 *
 * @return string
 *   The comma delimited results of the query.
 */
function drush_islandora_ri_search($query = NULL) {
  $repo = islandora_drush_repository();
  if (($file = drush_get_option('file')) !== NULL) {
    $query = file_get_contents($file);
  }
  else if ($query == NULL) {
    return drush_set_error(dt('No query argument or --file option given'));
  }
  $lang = drush_get_option('lang', 'itql');
  if (array_search($lang, array('itql', 'sparql')) === FALSE) {
    return drush_set_error(dt('Invalid argument for --lang !lang', array('!lang' => $lang)));
  }
  $limit = drush_get_option('limit', -1);
  $offset = drush_get_option('offset', 0);
  try {
    $results = $repo->ri->query($query, $lang, $limit, $offset);
    $result_values = function($o) { return $o['value']; };
    $results = array_map(function($o) use($result_values) { return array_map($result_values, $o); }, $results);
    drush_print("Query: $query");
    drush_print_table($results);
    $results = array_map(function($o) { return implode(',', $o); }, $results);
    drush_print_pipe($results);
    return TRUE;
  } catch (Exception $e) {
    return drush_set_error(dt('RI Search Query Failed: !msg', array('!msg' => $e->getMessage())));
  }
}

/**
 * Validate the arguments provided to this drush command.
 */
function drush_islandora_ri_search_validate() {
  return islandora_drush_include_tuque(); // @todo add additional checks.
}

/**
 * Includes Tuque API.
 *
 * @return Boolean
 *   TRUE if the API was included, FALSE otherwise.
 */
function islandora_drush_include_tuque() {
  if (!file_exists('sites/all/libraries/tuque')) {
    return drush_set_error(DRUSH_FRAMEWORK_ERROR, dt('The Tuque API was not found and it is a requirement for use of the islandora drush commands.'));
  }
  @include_once 'sites/all/libraries/tuque/Datastream.php';
  @include_once 'sites/all/libraries/tuque/FedoraApi.php';
  @include_once 'sites/all/libraries/tuque/FedoraApiSerializer.php';
  @include_once 'sites/all/libraries/tuque/Object.php';
  @include_once 'sites/all/libraries/tuque/RepositoryConnection.php';
  @include_once 'sites/all/libraries/tuque/Cache.php';
  @include_once 'sites/all/libraries/tuque/RepositoryException.php';
  @include_once 'sites/all/libraries/tuque/Repository.php';
  @include_once 'sites/all/libraries/tuque/FedoraRelationships.php';
  return TRUE;
}

/**
 * Fetches the Repository instance, creates it if it doesn't exist.
 *
 * @return FedoraRepository
 *   The default fedora repository for this site.
 */
function islandora_drush_repository() {
  static $repository = NULL;
  if (!isset($repository)) {
    global $user;
    $default_repo_url = 'http://localhost:8080/fedora';
    $repo_url_variable = (drush_drupal_major_version() >= 7) ? 'islandora_base_url' : 'fedora_base_url';
    $connection = new RepositoryConnection(variable_get($repo_url_variable, $default_repo_url),  $user->name, $user->pass);
    $connection->reuseConnection = FALSE;
    $api = new FedoraApi($connection);
    $cache = new SimpleCache();
    $repository = new FedoraRepository($api, $cache);
  }
  return $repository;
}

/**
 * Gets the input from STDIN.
 *
 * @return string
 *   The data passed into this command via STDIN
 */
function islandora_read_stdin_line() {
  stream_set_blocking(STDIN, FALSE);
  $line = fgets(STDIN);
  stream_set_blocking(STDIN, TRUE);
  return $line ? trim($line) : FALSE;
}
