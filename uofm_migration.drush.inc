<?php

function uofm_migration_drush_command() {
  return array(
    'adjust-models' => array(
      'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_LOGIN, // We can pass in users id on the command line using drush -u.
    ),
  );
}

function uofm_migration_map_to_pids($result) {
  return $result['object']['value'];
}

function drush_uofm_migration_adjust_models() {
  $source = islandora_get_tuque_connection();

  $transforms = array(
    'islandora:sp_basic_image' => 'sameTerm(?model, <info:fedora/uofm:compoundPrairieBuildingImage>) ||
sameTerm(?model, <info:fedora/uofm:compoundLowResImage>)',
    'islandora:sp_large_image_cmodel' => 'sameTerm(?model, <info:fedora/uofm:compoundHighResImage>)',
    'islandora:compoundCModel' => 'sameTerm(?model, <info:fedora/uofm:prairieBuilding>) ||
sameTerm(?model, <info:fedora/uofm:highResImage>) ||
sameTerm(?model, <info:fedora/uofm:lowResImage>)',
    'islandora:bookCModel' => 'sameTerm(?model, <info:fedora/archiveorg:bookCModel>)',
    'islandora:pageCModel' => 'sameTerm(?model, <info:fedora/archiveorg:pageCModel>)',
  );

  foreach ($transforms as $destination => $filter) {
    $initial_compound_pids = array_map('uofm_migration_map_to_pids', $source->repository->ri->sparqlQuery(<<<EOQ
SELECT ?object
FROM <#ri>
WHERE {
  ?object <fedora-model:hasModel> ?model .
  FILTER($filter)
}
ORDER BY ?object
EOQ
  ));

    $destination_compound_pids = array_map('uofm_migration_map_to_pids', $source->repository->ri->sparqlQuery(<<<EOQ
SELECT ?object
FROM <#ri>
WHERE {
  ?object <fedora-model:hasModel> ?model ;
          <fedora-model:hasModel> <info:fedora/$destination> .
  FILTER($filter)
}
ORDER BY ?object
EOQ
  ));

    $pids_to_compound = array_diff($initial_compound_pids, $destination_compound_pids);

    $total = count($pids_to_compound);

    drush_log(dt("Adding @uri... Source: @s\nDestination: @d\nTo process: @t", array(
      '@uri' => $destination,
      '@s' => count($initial_compound_pids),
      '@d' => count($destination_compound_pids),
      '@t' => $total,
    )));

    $i = 0;
    foreach ($pids_to_compound as $to_compound) {
      drush_log(dt('Adding model @uri to @pid. @i/@t', array(
        '@uri' => $destination,
        '@pid' => $to_compound,
        '@i' => ++$i,
        '@t' => $total,
      )));
      $obj = islandora_object_load($to_compound);
      if ($obj) {
        $obj->models = $destination;
      }
    }
  }
}

