<?php

function uofm_migration_drush_command() {
  return array(
    'adjust-models' => array(
      'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_LOGIN, // We can pass in users id on the command line using drush -u.
    ),
    'generate-fits' => array(
      'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_LOGIN, // We can pass in users id on the command line using drush -u.
    ),
    'generate-hocr' => array(
      'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_LOGIN, // We can pass in users id on the command line using drush -u.
    ),
    'generate-dimensions' => array(
      'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_LOGIN, // We can pass in users id on the command line using drush -u.
    ),
    'move-datastreams' => array(
      'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_LOGIN, // We can pass in users id on the command line using drush -u.
    ),
    'change-predicates' => array(
      'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_LOGIN, // We can pass in users id on the command line using drush -u.
    ),
  );
}

function uofm_migration_map_to_pids($result) {
  return $result['object']['value'];
}

function uofm_migration_stash_pids($pids, &$context) {
  $context['results']['pids'] = $pids;
}

function uofm_migration_batch_op($action, $message, &$context) {
  if (!isset($context['sandbox']['progress'])) {
    $context['sandbox']['progress'] = 0;
    $context['sandbox']['total'] = count($context['results']['pids']);
  }
  //dlm($context['results']);

  $i = &$context['sandbox']['progress'];
  $total = &$context['sandbox']['total'];

  $pid = $context['results']['pids'][$i];

  drush_log(format_string($message, array(
    '@pid' => $pid,
    '@i' => ++$i,
    '@t' => $total,
  )));
  call_user_func($action, $pid);

  $context['finished'] = $i / $total;
}

function uofm_migration_process_queries($source, $destination, $start_message, $individual_message, $action) {
  $connection = islandora_get_tuque_connection();

  $initial_process_pids = array_map('uofm_migration_map_to_pids', $connection->repository->ri->sparqlQuery($source));
  $destination_process_pids = array_map('uofm_migration_map_to_pids', $connection->repository->ri->sparqlQuery($destination));

  $pids_to_process =array_values(array_diff($initial_process_pids, $destination_process_pids));

  $total = count($pids_to_process);

  drush_log(format_string("@message Source: @s\nDestination: @d\nTo process: @t", array(
    '@message' => $start_message,
    '@s' => count($initial_process_pids),
    '@d' => count($destination_process_pids),
    '@t' => $total,
  )));

  if ($total > 0) {
    $batch = array(
      'title' => 'Batching...',
      'operations' => array(
        array('uofm_migration_stash_pids', array($pids_to_process)),
        array('uofm_migration_batch_op', array($action, $individual_message)),
      ),
    );

    batch_set($batch);
    drush_backend_batch_process();
  }
}


function drush_uofm_migration_adjust_models() {
  $transforms_to_add = array(
    'islandora:sp_basic_image' => 'sameTerm(?model, <info:fedora/uofm:compoundPrairieBuildingImage>) ||
sameTerm(?model, <info:fedora/uofm:compoundLowResImage>) ||
((sameTerm(?model, <info:fedora/uofm:lowResImage>) || sameTerm(?model, <info:fedora/uofm:prairieBuildingImage>)) && sameTerm(?datastream, <info:fedora/*/OBJ>) && !bound(?child))',
    'islandora:sp_large_image_cmodel' => 'sameTerm(?model, <info:fedora/uofm:compoundHighResImage>) ||
(sameTerm(?model, <info:fedora/uofm:highResImage>) && sameTerm(?datastream, <info:fedora/*/OBJ>) && !bound(?child))',
    'islandora:compoundCModel' => '(sameTerm(?model, <info:fedora/uofm:prairieBuilding>) ||
sameTerm(?model, <info:fedora/uofm:highResImage>) ||
sameTerm(?model, <info:fedora/uofm:lowResImage>)) && bound(?child)',
    'islandora:bookCModel' => 'sameTerm(?model, <info:fedora/archiveorg:bookCModel>)',
    'islandora:pageCModel' => 'sameTerm(?model, <info:fedora/archiveorg:pageCModel>)',
  );

  $transforms_to_remove = array(
    'islandora:compoundCModel' => '(sameTerm(?model, <info:fedora/uofm:prairieBuilding>) ||
sameTerm(?model, <info:fedora/uofm:highResImage>) ||
sameTerm(?model, <info:fedora/uofm:lowResImage>)) && !bound(?child)',
  );

  foreach ($transforms_to_add as $destination => $filter) {
    $source_query = <<<EOQ
SELECT ?object
FROM <#ri>
WHERE {
  ?object <fedora-model:hasModel> ?model ;
          <fedora-view:disseminates> ?dss .
  ?dss <fedora-view:disseminationType> ?datastream .
  OPTIONAL {
    ?child <fedora-rels-ext:isPartOf> ?object .
  }
  FILTER($filter)
}
ORDER BY ?object
EOQ;

    $destination_query = <<<EOQ
SELECT ?object
FROM <#ri>
WHERE {
  ?object <fedora-model:hasModel> ?model ;
          <fedora-model:hasModel> <info:fedora/$destination> ;
          <fedora-view:disseminates> ?dss .
  ?dss <fedora-view:disseminationType> ?datastream .
  OPTIONAL {
    ?child <fedora-rels-ext:isPartOf> ?object .
  }
  FILTER($filter)
}
ORDER BY ?object
EOQ;

    uofm_migration_process_queries($source_query, $destination_query, dt("Adding @uri...", array(
      '@uri' => $destination,
    )), dt('@pid (@i/@t).'), function ($pid) use ($destination) {
      $obj = islandora_object_load($pid);
      if ($obj) {
        $obj->models = $destination;
      }
    });
  }

  foreach ($transforms_to_remove as $destination => $filter) {
    $source_query = <<<EOQ
SELECT ?object
FROM <#ri>
WHERE {
  ?object <fedora-model:hasModel> ?model ;
          <fedora-model:hasModel> <info:fedora/$destination> ;
          <fedora-view:disseminates> ?dss .
  ?dss <fedora-view:disseminationType> ?datastream .
  OPTIONAL {
    ?child <fedora-rels-ext:isPartOf> ?object .
  }
  FILTER($filter)
}
ORDER BY ?object
EOQ;

    // XXX: A query to find nothing... Great fun.
    $destination_query = <<<EOQ
SELECT ?object
FROM <#ri>
WHERE {
  ?object <fedora-model:hasModel> ?model ;
          <find-nothing:real-good> 'asdf' ;
          <fedora-view:disseminates> ?dss .
  ?dss <fedora-view:disseminationType> ?datastream .
  OPTIONAL {
    ?child <fedora-rels-ext:isPartOf> ?object .
  }
  FILTER(($filter) && !sameTerm(?model, <info:fedora/$destination>))
}
ORDER BY ?object
EOQ;

    uofm_migration_process_queries($source_query, $destination_query, dt("Removing @uri...", array(
      '@uri' => $destination,
    )), dt('@pid (@i/@t).'), function ($pid) use ($destination) {
      $obj = islandora_object_load($pid);
      if ($obj) {
        $obj->relationships->remove(FEDORA_MODEL_URI, 'hasModel', $destination);
      }
    });
  }
}

function drush_uofm_migration_move_datastreams() {
  $transforms = array(
    array(
      'content_model' => 'islandora:sp_basic_image',
      'source_datastream_id' => 'JPG',
      'destination_datastream_id' => 'MEDIUM_SIZE',
    ),
    array(
      'content_model' => 'islandora:pageCModel',
      'source_datastream_id' => 'TIFF',
      'destination_datastream_id' => 'OBJ',
    ),
  );

  foreach ($transforms as $info) {
    extract($info);
    $source_query = <<<EOQ
SELECT ?object
FROM <#ri>
WHERE {
  ?object <fedora-model:hasModel> <info:fedora/$content_model> ;
          <fedora-view:disseminates> [<fedora-view:disseminationType> <info:fedora/*/$source_datastream_id>] .
}
ORDER BY ?object
EOQ;

    $destination_query = <<<EOQ
SELECT ?object
FROM <#ri>
WHERE {
  ?object <fedora-model:hasModel> <info:fedora/$content_model> ;
          <fedora-view:disseminates> [<fedora-view:disseminationType> <info:fedora/*/$source_datastream_id>] ;
          <fedora-view:disseminates> [<fedora-view:disseminationType> <info:fedora/*/$destination_datastream_id>] .
}
ORDER BY ?object
EOQ;

    $temp_file = drupal_tempnam('temporary://', 'ds_moving');

    uofm_migration_process_queries($source_query, $destination_query, dt("Copying @source to @destination...", array(
      '@source' => $source_datastream_id,
      '@destination' => $destination_datastream_id,
    )), dt('Copying @pid (@i/@t).'), function ($pid) use ($source_datastream_id, $destination_datastream_id, $temp_file) {
      $obj = islandora_object_load($pid);
      $src_ds = $obj[$source_datastream_id];
      $dest = $destination_datastream_id;

      if (!isset($obj[$dest])) {
        $dest_ds = $obj->constructDatastream($dest, $src_ds->controlGroup);
        $properties = array('checksumType', 'checksum', 'format', 'mimetype', 'versionable', 'label', 'state');
        $external = in_array($src_ds->controlGroup, array('R', 'E'));
        if ($external) {
          $properties[] = 'url';
        }
        else {
          $src_ds->getContent($temp_file);
          $dest_ds->setContentFromFile($temp_file, FALSE);
        }
        $copy_property = function($property) use($src_ds, $dest_ds) {
          $dest_ds->$property = $src_ds->$property;
        };
        array_walk($properties, $copy_property);
        $asdf = array(
          '@source' => $source_datastream_id,
          '@dest' => $destination_datastream_id,
          '@pid' => $pid,
        );
        if ($obj->ingestDatastream($dest_ds) == FALSE) {
          drush_log(dt('Failed to copy @source to @dest for @pid.', $asdf), 'warning');
        }
        else {
          drush_log(dt('Copied @source to @dest for @pid.', $asdf));
        }
      }
      else {
        drush_log(format_string('@pid already has @dest, not copying.'), 'warning');
      }
    });
  }
}

function drush_uofm_migration_change_predicates() {
  $transforms = array(
    array(
      'content_model' => 'islandora:pageCModel',
      'source_uri' => 'info:islandora/islandora-system:def/pageinfo#',
      'source_predicate' => 'isPageNumber',
      'destination_uri' => 'http://islandora.ca/ontology/relsext#',
      'destination_predicate' => 'isSequenceNumber',
    ),
  );

  foreach ($transforms as $info) {
    extract($info);
    $source_query = <<<EOQ
SELECT ?object
FROM <#ri>
WHERE {
  ?object <fedora-model:hasModel> <info:fedora/$content_model> ;
          <$source_uri$source_predicate> ?value .
}
ORDER BY ?object
EOQ;

    $destination_query = <<<EOQ
SELECT ?object
FROM <#ri>
WHERE {
  ?object <fedora-model:hasModel> <info:fedora/$content_model> ;
          <$source_uri$source_predicate> ?value ;
          <$destination_uri$destination_predicate> ?value .
}
ORDER BY ?object
EOQ;

    uofm_migration_process_queries($source_query, $destination_query, dt("Copying predicate @source to @destination...", array(
      '@source' => $source_predicate,
      '@destination' => $destination_predicate,
    )), dt('Changing predicate on @pid (@i/@t).'), function ($pid) use ($info) {
      extract($info);
      $obj = islandora_object_load($pid);
      $old = reset($obj->relationships->get($source_uri, $source_predicate));
      if ($old) {
        $obj->relationships->add($destination_uri, $destination_predicate, $old['object']['value'], $old['object']['literal']);
      }
    });
  }
}

function uofm_migration_generate_fits($pid) {
  $obj = islandora_object_load($pid);
  if ($obj) {
    module_load_include('inc', 'islandora_fits', 'includes/utilities');
    islandora_fits_create_techmd($obj);
  }
}

function drush_uofm_migration_generate_fits() {
  $source = <<<EOQ
SELECT ?object
FROM <#ri>
WHERE {
  ?object <fedora-view:disseminates> [<fedora-view:disseminationType> <info:fedora/*/EXIF>] ;
          <fedora-view:disseminates> [<fedora-view:disseminationType> <info:fedora/*/OBJ>] .
}
EOQ;
  $dest = <<<EOQ
SELECT ?object
FROM <#ri>
WHERE {
  ?object <fedora-view:disseminates> [<fedora-view:disseminationType> <info:fedora/*/TECHMD>] .
}
EOQ;

  uofm_migration_process_queries($source, $dest, dt("Generating FITS (TECHMD stream)..."), dt('@pid (@i/@t).'), 'uofm_migration_generate_fits');
}

function uofm_migration_generate_hocr($pid) {
  $obj = islandora_object_load($pid);
  if ($obj) {
    module_load_include('inc', 'islandora_paged_content', 'includes/utilities');
    islandora_paged_content_page_derive_ocr_datastreams($obj);
  }
}

function drush_uofm_migration_generate_hocr() {
  $source = <<<EOQ
SELECT ?object
FROM <#ri>
WHERE {
  ?object <fedora-model:hasModel> <info:fedora/islandora:pageCModel> .
}
EOQ;
  $dest = <<<EOQ
SELECT ?object
FROM <#ri>
WHERE {
  ?object <fedora-model:hasModel> <info:fedora/islandora:pageCModel> ;
          <fedora-view:disseminates> [<fedora-view:disseminationType> <info:fedora/*/HOCR>] .
}
EOQ;

  uofm_migration_process_queries($source, $dest, dt("Generating HOCR..."), dt('@pid (@i/@t).'), 'uofm_migration_generate_hocr');
}

function uofm_migration_generate_dimensions($pid) {
  $obj = islandora_object_load($pid);
  module_load_include('inc', 'islandora_paged_content', 'includes/utilities');
  if ($obj) {
    module_load_include('inc', 'islandora_internet_archive_bookreader', 'includes/utilities');
    $metadata = islandora_internet_archive_bookreader_get_technical_metadata($obj);
    if ($metadata) {
      extract($metadata);
    }
    else {
      $temp_file = drupal_tempnam('temporary://', 'dimensions');
      $obj['OBJ']->getContent($temp_file);
      list($width, $height) = getimagesize($temp_file);
      drupal_unlink($temp_file);
    }

    $rels = $obj['JP2']->relationships;
    islandora_paged_content_set_relationship($rels, ISLANDORA_RELS_EXT_URI,
      'width', $width, 1
    );
    islandora_paged_content_set_relationship($rels, ISLANDORA_RELS_EXT_URI,
      'height', $height, 1
    );
  }
}

function drush_uofm_migration_generate_dimensions() {
  $source = <<<EOQ
SELECT ?object
FROM <#ri>
WHERE {
  ?object <fedora-model:hasModel> <info:fedora/islandora:pageCModel> ;
          <fedora-view:disseminates> [<fedora-view:disseminationType> <info:fedora/*/OBJ>]
}
EOQ;
  $dest = <<<EOQ
PREFIX islandora-rels-ext: <http://islandora.ca/ontology/relsext#>
SELECT ?object
FROM <#ri>
WHERE {
  ?object <fedora-model:hasModel> <info:fedora/islandora:pageCModel> ;
          <fedora-view:disseminates> ?dss .
  ?dss <fedora-view:disseminationType> <info:fedora/*/JP2> ;
       islandora-rels-ext:width ?width ;
       islandora-rels-ext:height ?height .
}
EOQ;

  uofm_migration_process_queries($source, $dest, dt("Generating HOCR..."), dt('@pid (@i/@t).'), 'uofm_migration_generate_dimensions');
}
