<?php

/**
 * @file
 *
 * A collection of Drush commands to make managing islandora a bit easier.
 */

/**
 * Implements hook_drush_command.
 */
function islandora_drush_command() {
  return array(
    'islandora-purge' => array(
      'callback' => 'drush_islandora_purge',
      'arguments' => array('recursive' => 'Purge all child and decendant objects as well.'),
      'description' => 'Purges the object identified by the provided PID',
      'examples' => array(
        'drush islandora-purge PID' => 'Delete the object identified by PID',
        'drush islandora-purge --recursive PID' => 'Delete the object identified by PID as well as all decendant elements defined by isMemberOf or isMemberOfCollection relationships',
      ),
      'aliases' => array('purge'),
      'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_LOGIN, // We can pass in users id on the command line using drush -u.
    ),
    // @todo Implement
    'islandora-set-state' => array(
      'callback' => 'drush_islandora_set_state',
      'description' => 'Changes the given state of the object idenitified by the provided PID. The options are "A", "I", "D"',
      'examples' => array(
        'drush islandora-set-state PID STATE',
      ),
      'aliases' => array('set-state'),
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
  $repo = islandora_drush_repository();
  if(drush_get_option('recursive')) {
    $query = 'select $o from <#ri> where ($o <fedora-rels-ext:isMemberOf> <info:fedora/' . $pid . '> or $o <fedora-rels-ext:isMemberOfCollection> <info:fedora/' . $pid . '>)';
    $results = $repo->ri->itqlQuery($query);
    $results = array_map(function($o) { return $o['o']['value']; }, $results);
    array_walk($results, 'drush_islandora_purge');
  }
  try {
    $o = $repo->getObject($pid); // Check if the object exists.
    $repo->api->m->purgeObject($o->id, false, 'Purged object via islandora drush purge script.');
  } catch (Exception $e) {
    drush_set_error(dt('Failed to purge the object "!PID": !msg', array('!PID' => $pid, '!msg' => $e->getMessage())));
    return;
  }
  drush_print(dt('Successfully purge the object "!PID"', array('!PID' => $pid)));
}

/**
 * Validate the arguments provided to this drush command.
 */
function drush_islandora_purge_validate() {
  return islandora_drush_include_tuque(); // @todo add additional checks.
}

/**
 * Includes Tuque API.
 *
 * @return boolean
 *   TRUE if the API was included, FALSE otherwise.
 */
function islandora_drush_include_tuque() {
  if(!file_exists('sites/all/libraries/tuque')) {
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
 * Fetchs the Repository instance, creates it if it doesn't exist.
 *
 * @return FedoraRepository
 *   The default fedora repository for this site.
 */
function islandora_drush_repository() {
  static $repository = NULL;
  if(!isset($repository)) {
    global $user;
    $default_repo_url = 'http://localhost:8080/fedora';
    $repo_url_variable = (drush_drupal_major_version() >= 7) ? 'islandora_base_url' : 'fedora_base_url';
    $connection = new RepositoryConnection(variable_get($repo_url_variable, $default_repo_url),  $user->name, $user->pass);
    $connection->reuseConnection = TRUE;
    $api = new FedoraApi($connection);
    $cache = new SimpleCache();
    $repository = new FedoraRepository($api, $cache);
  }
  return $repository;
}