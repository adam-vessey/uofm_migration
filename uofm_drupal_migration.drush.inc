<?php

function uofm_drupal_migration_drush_command() {
  return array(
    'migrate-fedora' => array(
      'options' => array(
        'source' => array(
          'description' => 'The source Fedora (http://whatever:8080/fedora)',
          'required' => TRUE,
        ),
        'source_user' => array(
          'description' => 'An admin user on the source system we shall use.',
          'value' => 'optional',
        )
      ),
      'description' => 'Copy objects from the source to the destination.',
      'aliases' => array('mf'),
      'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_LOGIN, // We can pass in users id on the command line using drush -u.
    ),
  );
}

function drush_uofm_drupal_migration_migrate_fedora() {
  $source_url = drush_get_option('source');
  $source_user = new stdClass();
  // Not 0, so it uses the name and pass. ;P
  $source_user->uid = -1;
  $source_user->name = drush_get_option('source_user', FALSE);
  if (!$source_user->name) {
     $source_user->name = drush_prompt('Enter an admin user on the source system:', 'fedoraAdmin', TRUE);
  }
  $source_user->pass = drush_prompt('Enter the password for the admin user:', 'fedoraAdmin', TRUE, TRUE);

  $source = new IslandoraTuque($source_user, $source_url);

  $destination = islandora_get_tuque_connection();

  $query = <<<EOQ
SELECT ?object
FROM <#ri>
WHERE {
  ?object <fedora-model:hasModel> <info:fedora/fedora-system:FedoraObject-3.0> .
}
ORDER BY ?object
EOQ;

  $map_to_pids = function($result) {
    return $result['object']['value'];
  };

  //Query for the source objects...
  dlm(dt('Performing source query...'));
  $source_results = $source->repository->ri->sparqlQuery($query);
  $source_results = array_map($map_to_pids, $source_results);
  dlm(format_plural(count($source_results), 'Source has 1 object.', 'Source has @count objects.'));

  //Query for the source objects in the destination...
  dlm(dt('Performing destination query...'));
  $destination_results = $destination->repository->ri->sparqlQuery($query);
  $destination_results = array_map($map_to_pids, $destination_results);
  dlm(format_plural(count($destination_results), 'Destination has 1 object.', 'Destination has @count objects.'));

  //For each object not in the destination, grab and throw
  $missing_in_destination = array_diff($source_results, $destination_results);
  dlm(format_plural(count($missing_in_destination), 'Source has 1 object which does not exist in the destination.',
    'Source has @count objects which do not exist in the destination.'));
  foreach ($missing_in_destination as $missing_pid) {
    dlm(dt('Missing @pid. Exporting from source...', array('@pid' => $missing_pid)));
    $response = $source->repository->api->m->export($missing_pid, array(
      'format' => 'info:fedora/fedora-system:FOXML-1.1',
      'context' => 'migrate',
    ));

    dlm(dt('Got export; about to ingest.'));

    $result = $destination->repository->api->m->ingest(array(
      'string' => $response,
    ));
    if ($result) {
      dlm(dt('Migrated @pid.', array('@pid' => $missing_pid)));
    }
    else {
      dlm(dt('Failed to migrate @pid.', array('@pid' => $missing_pid)));
    }
  }
}
