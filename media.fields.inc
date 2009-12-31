<?php
// $Id$

/**
 * Implement hook_field_info().
 *
 */
function media_field_info() {
  return array(
    'media' => array(
      'label' => t('Multimedia asset'),
      'description' => t('This field stores a reference to a multimedia asset.'),
      'settings' => array(),
      'instance_settings' => array(),
      'default_widget' => 'media_generic',
      'default_formatter' => 'media_generic',
    ),
  );
}

/**
 * Implements hook_field_schema().
 */
function media_field_schema($field) {
  return array(
    'columns' => array(
      'fid' => array(
        'type' => 'int',
        'unsigned' => TRUE,
        'not null' => FALSE,
      ),
    ),
    'indexes' => array(
      'fid' => array('fid'),
    ),
  );
}


/**
 * Implements hook_field_is_empty().
 */
function media_field_is_empty($item, $field) {
  if (!is_array($item) || (empty($item['fid']))) {
    return TRUE;
  }
  return FALSE;
}


/**
 * Implement hook_field_widget_info().
 */
function media_field_widget_info() {
  return array(
    'media_generic' => array(
      'label' => t('File (media)'),
      'field types' => array('file', 'media'),
      'settings' => array(
        'progress_indicator' => 'throbber',
        'allowed_types' => array('image'),
        'allowed_schemes' => array('public', 'private'),
      ),
      'behaviors' => array(
        'multiple values' => FIELD_BEHAVIOR_CUSTOM,
        'default value' => FIELD_BEHAVIOR_NONE,
      ),
    ),
  );
}

/**
 * Implement hook_field_formatter_info
 */
function media_field_formatter_info() {
  $formatters = array();
  $build_modes = media_field_view_modes('media');
  foreach ($build_modes as $key => $mode_name) {
    $formatters[$key] = array(
      'label' => t($mode_name),
      'field types' => array('media'),
    );
  }
  return $formatters;
}


/**
 * Used to Implement hook_field_view_modes().
 */
function media_field_view_modes($obj_type) {
  $modes = array();
  if ($obj_type == 'media') {
    $modes = array(
      'preview' => array('label' => t('Preview')),
      'small' => array('label' => t('Small')),
      'large' => array('label' => t('Large')),
      'original' => array('label' => t('Original')),
    );
  }
  return $modes;
}

function media_field_ui_view_modes_tabs() {

  $modes = array(
    'basic' => array(
      'view modes' => array_keys(media_field_view_modes('media')),
    ),
  );
  return $modes;
}


/**
 * Implement hook_field_formatter_view
 */
function media_field_formatter_view($obj_type, $object, $field, $instance, $langcode, $items, $display) {
  $element = array();

  foreach ($items as $delta => $item) {
    $file = media_load($item['fid']);
    dpr($file);
    dpr($display['type']);
    $element[$delta] = field_attach_view('media', $file, $display['type'], $langcode);
  }

//
//  switch ($display['type']) {
//    case 'media_default':
//      // Common case: each value is displayed individually in a sub-element
//      // keyed by delta. The field.tpl.php template specifies the markup
//      // wrapping each value.
//      foreach ($items as $delta => $item) {
//        $file = media_load($item['fid']);
//        $element[$delta] = array('#markup' => l($file->filename, file_create_url($file->uri)));
//        $build_mode = 'full';
//        $fields = field_attach_view('media', $file, $build_mode, $langcode);
//        $element[$delta] += $fields;
//        //@todo: write a proper theming function for this.
//      }
//      break;
//
//    case 'media_teaser':
//      // The question here is how to we show fields along w/ media.
//      // Perhaps it should happen on every object type?
//
//      break;
//  }

  return $element;
}


/**
 * Implement hook_field_widget_settings_form().
 */
function media_field_widget_settings_form($field, $instance) {

  $widget = $instance['widget'];
  $settings = $widget['settings'];
  $form = array();

  // Setup type selection form
  $types = media_type_get_types();
  $options = array();
  foreach ($types as $key => $definition) {
    $options[$key] = $definition->label;
  }

  $form['allowed_types'] = array (
    '#type' => 'checkboxes',
    '#title' => t('Allowed media types'),
    '#options' => $options,
    '#default_value' => $settings['allowed_types'],
    '#description' => t('Media types which are allowed for this field'),
    '#weight' => 1,
  );

  $streams = file_get_stream_wrappers();
  $options = array();
  unset($streams['temporary']);
  foreach ($streams as $scheme => $data) {
    $options[$scheme] = t('@scheme (@name)', array('@scheme' => $scheme . '://', '@name' => $data['name']));
  }
  $form['allowed_schemes'] = array(
    '#type' => 'checkboxes',
    '#title' => t('Allowed URI schemes'),
    '#options' => $options,
    '#default_value' => $settings['allowed_schemes'],
    '#description' => t('URI schemes include public:// and private:// which are the Drupal files directories, and may also refer to remote sites.'),
    '#weight' => 2,
  );
  return $form;
}

/**
 *  Implement hook_field_widget_form().
 */
function media_field_widget_form(&$form, &$form_state, $field, $instance, $langcode, $items, $delta = 0) {
  $widget_settings = $instance['widget']['settings'];

  $permission = user_access('use media browser');
  if (!$permission) {
    // @todo: show the default file upload field.
    return;
  }

  // Retrieve any values set in $form_state, as will be the case during AJAX
  // rebuilds of this form.
  if (isset($form_state['values'][$field['field_name']][$langcode])) {
    $items = $form_state['values'][$field['field_name']][$langcode];
    unset($form_state['values'][$field['field_name']][$langcode]);
  }

  // Re-index deltas after removing empty items.
  $items = array_values($items);

  // Update order according to weight.
  $items = _field_sort_items($field, $items);

  $element = array(
    '#theme' => 'media_managed_file',
    '#type' => 'media_managed_file',
    '#default_value' => isset($items[$delta]) ? $items[$delta] : 0,
    '#required' => $instance['required'],
    //'#media_file_extensions' => $instance['settings']['file_extensions'],
    '#media_allowed_types' => $instance['widget']['settings']['allowed_types'],
    '#media_allowed_schemes' => $instance['widget']['settings']['allowed_schemes'],
    '#process' => array('media_generic_widget_process'),
    // Add extra Field properties.
    '#field_name' => $field['field_name'],
    '#bundle' => $instance['bundle'],
    '#object_type' => $instance['object_type'],
  );

  if ($field['cardinality'] == 1) {
    // If there's only one field, return it as delta 0.
    $element['#title'] = $instance['label'];
    $elements = array($element);
  }
  else {
    // If there are multiple values, add an element for each existing one.
    $delta = -1;
    foreach ($items as $delta => $item) {
      $elements[$delta] = $element;
      $elements[$delta]['#default_value'] = $item;
      $elements[$delta]['#weight'] = $delta;
    }
    // And then add one more empty row for new uploads.
    $delta++;
    if ($field['cardinality'] == FIELD_CARDINALITY_UNLIMITED || $delta < $field['cardinality']) {
      $elements[$delta] = $element;
      $elements[$delta]['#default_value'] = 0;
      $elements[$delta]['#weight'] = $delta;
      $elements[$delta]['#required'] = ($instance['required'] && $delta == 0);
    }
    // The group of elements all-together need some extra functionality
    // after building up the full list (like draggable table rows).
    $elements['#file_upload_delta'] = $delta;

    $elements['#theme_wrappers'] = array('fieldset');
    $elements['#attributes']['class'] = array('media-file-widget');

    $elements['#title'] = $instance['label'];
    $elements['#description'] = $instance['description'];

  }

  return $elements;
}


/**
 * An element #process callback for the media_managed_file type.
 *
 * Expands the media_managed_file type to include the uri field.
 */
function media_generic_widget_process($element, &$form_state, $form) {
  $fid = isset($element['#default_value']['fid']) ? $element['#default_value']['fid'] : 0;
  $file = media_load($fid);

  $path = drupal_get_path('module', 'media');

  if ($file) {
    $element['filename'] = array(
      '#type' => 'item',
      '#title' => $file->filename,
      '#markup' => '',
      '#prefix' => '<h3 class="media-filename">',
      '#suffix' => '</h3>',
    );
  }

  //@TODO: This should be loaded from the JS in case of a failed form submission.

  $element['preview'] = array(
    '#type' => 'item',
    '#markup' => theme('media_admin_thumbnail', array('file' => $file, 'style_name' => 'thumbnail')),
    '#prefix' => '<div class="preview">',
    '#suffix' => '</div>',
  );

  //@HACK: @todo: this is so I can find it in media.js without putting every field in a settings variable.
  // This is hidden in media.css
  $element['fid'] = array(
    '#type' => 'textfield',
    '#default_value' => $fid
  );

  // JS: This is just 1/2 written code of what launching the browser might look like
  $element['launcher'] = array (
    '#type' => 'markup',
    '#markup' => '<a class="launcher" href="#">Select Media</a>',
  );

  if (variable_get('media_debug', TRUE)) {
    drupal_add_js(array('media' => array('debug' => TRUE)), 'setting');
    $element['debug'] = array(
      '#type' => 'fieldset',
      '#title' => 'Debug information',
      '#collapsed' => FALSE,
      '#collapsible' => TRUE,
    );

    $element['debug']['file_info'] = array(
      '#type' => 'textarea',
      '#rows' => 5,
      '#title' => 'File Info',
      '#attributes' => array('class' => array('file_info'), 'style' => 'font-size:.75em; font-family:courier'),
      '#default_value' => drupal_json_encode($file),
    );
  }

  $element['fid']['#attributes'] = array('class' => array('fid'));

  // @todo: make me launch a simple iframe

  // Add some JS Settings
  // Not currently in effect.
  $browser_settings = array(
    'media_items_callback' => url('media/service/get/for/fields'),
    // These params are passed on every callback
    'extra_params' => array (
      'bundle' => $element['#bundle'],
      'field_name' => $element['#field_name'],
      'object_type' => $element['#object_type'],
    ),
  );

  // Media browser attach code.
  media_attach_browser();

  return $element;
}

/**
 * Implements hook_field_validate().
 *
 * Possible error codes:
 * - 'media_fid_illegal_value': The value is not part of the list of allowed values.
 */
function media_field_validate($obj_type, $object, $field, $instance, $langcode, $items, &$errors) {

  $allowed_types = array_keys(array_filter($instance['widget']['settings']['allowed_types']));

    // @TODO: merge in stuff from media_uri_value
  foreach ($items as $delta => $item) {
    if ($item['fid'] == 0) {
      return TRUE;
      //@TODO: make support for submiting with just a URI here?
    }
    $result = db_select('file', 'f')
      ->fields('f')
      ->condition('fid', $item['fid'])
      ->condition('type', $allowed_types, 'IN')
      ->execute()
      ->fetchField();
    if (!$result) {
      $errors[$field['field_name']][$langcode][$delta][] = array(
        'error' => 'media_fid_illegal_value',
        'message' => t('%name: illegal value.', array('%name' => t($instance['label']))),
      );
    }
  }
}