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
    'sample-ocr-to-hocr' => array(
      'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_LOGIN, // We can pass in users id on the command line using drush -u.
    ),
    'regenerate-hocr-from-page' => array(
      'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_LOGIN, // We can pass in users id on the command line using drush -u.
    ),
  );
}

function _map_to_pids($result) {
  return $result['object']['value'];
}

function drush_uofm_drupal_migration_regenerate_hocr_from_page() {
  $source = islandora_get_tuque_connection();

  $source_results = $source->repository->ri->sparqlQuery(<<<EOQ
SELECT ?object
FROM <#ri>
WHERE {
  ?object <fedora-model:hasModel> <info:fedora/fedora-system:FedoraObject-3.0> ;
          <fedora-view:disseminates> ?dss .
  ?dss <fedora-view:disseminationType> <info:fedora/*/HOCR> ;
       <fedora-view:mimeType> 'text/xml' .
  FILTER(regex(str(?object), '^info:fedora/changeme:'))
}
ORDER BY ?object
EOQ
);

  $source_results = array_map('_map_to_pids', $source_results);
  $temp_file = drupal_tempnam('temporary://', 'tif');
  foreach($source_results as $pid) {
    $obj = islandora_object_load($pid);

    $xsl = new DOMDocument();
    $xsl->load(drupal_get_path('module', 'uofm_newspaper_batch') . '/transforms/iarchive_to_hocr.xsl');

    $input = new DOMDocument();
    $input->loadXML($obj['page']->content);

    $processor = new XSLTProcessor();
    $processor->importStylesheet($xsl);

    $base_hocr = $processor->transformToXML($input);

    $obj['OBJ']->getContent($temp_file);
    list($width, $height) = getimagesize($temp_file);

    $xml = new SimpleXMLElement($base_hocr);
    $xml->registerXPathNamespace('x', 'http://www.w3.org/1999/xhtml');
    $pages = $xml->xpath('/x:html/x:body/x:div[@class="ocr_page"][1]');
    if ($pages) {
      // Grab the page.
      $page = reset($pages);
      $page['title'] = format_string('image "!image"; bbox 0 0 !width !height', array(
        '!image' => $temp_file,
        '!width' => $width,
        '!height' => $height,
      ));
    }

    $obj['HOCR']->content = $xml->asXML();
    drush_log(dt('Regenerated @pid/HOCR.', array('@pid' => $pid)), 'info');
  }
  drupal_unlink($temp_file);
}

function drush_uofm_drupal_migration_sample_ocr_to_hocr() {
  $source = islandora_get_tuque_connection();

  $source_results = $source->repository->ri->sparqlQuery(<<<EOQ
SELECT ?object
FROM <#ri>
WHERE {
  ?object <fedora-model:hasModel> <info:fedora/fedora-system:FedoraObject-3.0> ;
          <fedora-view:disseminates> ?dss .
  ?dss <fedora-view:disseminationType> <info:fedora/*/OCR> ;
       <fedora-view:mimeType> 'text/xml' .
  FILTER(regex(str(?object), '^info:fedora/changeme:'))
}
ORDER BY ?object
EOQ
);
  $destination_results = $source->repository->ri->sparqlQuery(<<<EOQ
SELECT ?object
FROM <#ri>
WHERE {
  ?object <fedora-model:hasModel> <info:fedora/fedora-system:FedoraObject-3.0> ;
          <fedora-view:disseminates> ?dss .
  ?dss <fedora-view:disseminationType> <info:fedora/*/HOCR> .
  FILTER(regex(str(?object), '^info:fedora/changeme:'))
}
ORDER BY ?object
EOQ
);
  $source_results = array_map('_map_to_pids', $source_results);
  $destination_results = array_map('_map_to_pids', $destination_results);

  $missing_pids = array_diff($source_results, $destination_results);
  foreach ($missing_pids as $pid) {
    $obj = islandora_object_load($pid);
    $src_ds = $obj['OCR'];
    $dest = 'HOCR';

    $dest_ds = $obj->constructDatastream($dest, $src_ds->controlGroup);
    $properties = array('checksumType', 'checksum', 'format', 'mimetype', 'versionable', 'label', 'state');
    $external = array_search($src_ds->controlGroup, array('R', 'E')) !== FALSE;
    $properties[] = $external ? 'url' : 'content';
    $copy_property = function($property) use($src_ds, $dest_ds) { $dest_ds->$property = $src_ds->$property; };
    array_walk($properties, $copy_property);
    if ($obj->ingestDatastream($dest_ds) == FALSE) {
      dlm(dt('Failed to copy OCR for @pid.', array('@pid' => $pid)));
    }
    else {
      dlm(dt('Copied OCR for @pid.', array('@pid' => $pid)));
    }
  }
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


  //Query for the source objects...
  dlm(dt('Performing source query...'));
  $source_results = $source->repository->ri->sparqlQuery($query);
  $source_results = array_map('_map_to_pids', $source_results);
  dlm(format_plural(count($source_results), 'Source has 1 object.', 'Source has @count objects.'));

  //Query for the source objects in the destination...
  dlm(dt('Performing destination query...'));
  $destination_results = $destination->repository->ri->sparqlQuery($query);
  $destination_results = array_map('_map_to_pids', $destination_results);
  dlm(format_plural(count($destination_results), 'Destination has 1 object.', 'Destination has @count objects.'));

  //For each object not in the destination, grab and throw
  $missing_in_destination = array_diff($source_results, $destination_results);
  $total_missing = count($missing_in_destination);
  dlm(format_plural($total_missing, 'Source has 1 object which does not exist in the destination.',
    'Source has @count objects which do not exist in the destination.'));
  $i = 0;
  foreach ($missing_in_destination as $missing_pid) {
    $response = FALSE;
    try {
      dlm(dt('Missing @pid. Exporting from source...', array('@pid' => $missing_pid)));
      $response = $source->repository->api->m->export($missing_pid, array(
        'format' => 'info:fedora/fedora-system:FOXML-1.1',
        'context' => 'migrate',
      ));

      try {
        dlm(dt('Got export; about to ingest.'));
        $result = $destination->repository->api->m->ingest(array(
          'string' => $response,
        ));
        if ($result) {
          dlm(dt('Migrated @pid (@num/@total).', array(
            '@pid' => $missing_pid,
            '@num' => ++$i,
            '@total' => $total_missing,
          )));
        }
        else {
          dlm(dt('Failed to migrate @pid.', array('@pid' => $missing_pid)));
        }
      }
      catch (RepositoryException $e) {
        // Failed to import.
        dlm(dt('Failed to export @pid. Message: %message', array(
          '%message' => $e->getMessage(),
          '@pid' => $missing_pid,
        )));
      }
    }
    catch (RepositoryException $e) {
      // Failed to export.
      dlm(dt('Failed to import @pid. Message: %message', array(
        '%message' => $e->getMessage(),
        '@pid' => $missing_pid,
      )));
    }
  }
}
