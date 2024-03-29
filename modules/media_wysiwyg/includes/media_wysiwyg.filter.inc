<?php

/**
 * @file
 * Functions related to the WYSIWYG editor and the media input filter.
 */

define('MEDIA_WYSIWYG_TOKEN_REGEX', '/\[\[.*?\]\]/s');

/**
 * Filter callback for media markup filter.
 *
 * @TODO check for security probably pass text through filter_xss
 */
function media_wysiwyg_filter($text) {
  $text = preg_replace_callback(MEDIA_WYSIWYG_TOKEN_REGEX, 'media_wysiwyg_token_to_markup', $text);
  return $text;
}

/**
 * Parses the contents of a CSS declaration block.
 *
 * @param string $declarations
 *   One or more CSS declarations delimited by a semicolon. The same as a CSS
 *   declaration block (see http://www.w3.org/TR/CSS21/syndata.html#rule-sets),
 *   but without the opening and closing curly braces. Also the same as the
 *   value of an inline HTML style attribute.
 *
 * @return array
 *   A keyed array. The keys are CSS property names, and the values are CSS
 *   property values.
 */
function media_wysiwyg_parse_css_declarations($declarations) {
  $properties = array();
  foreach (array_map('trim', explode(";", $declarations)) as $declaration) {
    if ($declaration != '') {
      list($name, $value) = array_map('trim', explode(':', $declaration, 2));
      $properties[strtolower($name)] = $value;
    }
  }
  return $properties;
}

/**
 * Replace callback to convert a media file tag into HTML markup.
 *
 * @param string $match
 *   Takes a match of tag code
 * @param bool $wysiwyg
 *   Set to TRUE if called from within the WYSIWYG text area editor.
 *
 * @return string
 *   The HTML markup representation of the tag, or an empty string on failure.
 *
 * @see media_wysiwyg_get_file_without_label()
 * @see hook_media_wysiwyg_token_to_markup_alter()
 */
function media_wysiwyg_token_to_markup($match, $wysiwyg = FALSE) {
  $settings = array();
  $match = str_replace("[[", "", $match);
  $match = str_replace("]]", "", $match);
  $tag = $match[0];

  try {
    if (!is_string($tag)) {
      throw new Exception('Unable to find matching tag');
    }

    $tag_info = drupal_json_decode($tag);

    if (!isset($tag_info['fid'])) {
      throw new Exception('No file Id');
    }

    // Ensure the 'link_text' key is always defined.
    if (!isset($tag_info['link_text'])) {
      $tag_info['link_text'] = NULL;
    }

    // Ensure a valid view mode is being requested.
    if (!isset($tag_info['view_mode'])) {
      $tag_info['view_mode'] = variable_get('media__wysiwyg_default_view_mode', 'full');
    }
    elseif ($tag_info['view_mode'] != 'default') {
      $file_entity_info = entity_get_info('file');
      if (!in_array($tag_info['view_mode'], array_keys($file_entity_info['view modes']))) {
        // Media 1.x defined some old view modes that have been superseded by
        // more semantically named ones in File Entity. The media_update_7203()
        // function updates field settings that reference the old view modes,
        // but it's impractical to update all text content, so adjust
        // accordingly here.
        static $view_mode_updates = array(
          'media_preview' => 'preview',
          'media_small' => 'teaser',
          'media_large' => 'full',
        );
        if (isset($view_mode_updates[$tag_info['view_mode']])) {
          $tag_info['view_mode'] = $view_mode_updates[$tag_info['view_mode']];
        }
        else {
          throw new Exception('Invalid view mode');
        }
      }
    }

    $file = file_load($tag_info['fid']);
    if (!$file) {
      throw new Exception('Could not load media object');
    }
    $tag_info['file'] = $file;

    // The class attributes is a string, but drupal requires it to be
    // an array, so we fix it here.
    if (!empty($tag_info['attributes']['class'])) {
      $tag_info['attributes']['class'] = explode(" ", $tag_info['attributes']['class']);
    }

    // Grab the potentially overrided fields from the file.
    $fields = media_wysiwyg_filter_field_parser($tag_info);

    $attributes = is_array($tag_info['attributes']) ? $tag_info['attributes'] : array();
    $attribute_whitelist = variable_get('media__wysiwyg_allowed_attributes', _media_wysiwyg_wysiwyg_allowed_attributes_default());
    $settings['attributes'] = array_intersect_key($attributes, array_flip($attribute_whitelist));
    $settings['fields'] = $fields;

    if (!empty($tag_info['attributes']) && is_array($tag_info['attributes'])) {
      $settings['attributes'] = array_intersect_key($tag_info['attributes'], array_flip($attribute_whitelist));
      $settings['fields'] = $fields;

      // Many media formatters will want to apply width and height independently
      // of the style attribute or the corresponding HTML attributes, so pull
      // these two out into top-level settings. Different WYSIWYG editors have
      // different behavior with respect to whether they store user-specified
      // dimensions in the HTML attributes or the style attribute - check both.
      // Per http://www.w3.org/TR/html5/the-map-element.html#attr-dim-width, the
      // HTML attributes are merely hints: CSS takes precedence.
      if (isset($settings['attributes']['style'])) {
        $css_properties = media_wysiwyg_parse_css_declarations($settings['attributes']['style']);
        foreach (array('width', 'height') as $dimension) {
          if (isset($css_properties[$dimension]) && substr($css_properties[$dimension], -2) == 'px') {
            $settings[$dimension] = substr($css_properties[$dimension], 0, -2);
          }
          elseif (isset($settings['attributes'][$dimension])) {
            $settings[$dimension] = $settings['attributes'][$dimension];
          }
        }
      }
    }
  }
  catch (Exception $e) {
    watchdog('media', 'Unable to render media from %tag. Error: %error', array('%tag' => $tag, '%error' => $e->getMessage()));
    return '';
  }

  // If the tag has link text stored with it, override the filename with it for
  // the rest of this function, so that if the file is themed as a link, the
  // desired text will be used (see, for example, theme_file_link()).
  // @todo: Try to find a less hacky way to do this.
  if (isset($tag_info['link_text'])) {
    // The link text will have characters such as "&" encoded for HTML, but the
    // filename itself needs the raw value when it is used to build the link,
    // in order to avoid double encoding.
    $file->filename = decode_entities($tag_info['link_text']);
  }

  if ($wysiwyg) {
    $settings['wysiwyg'] = $wysiwyg;
    // If sending markup to a WYSIWYG, we need to pass the file infomation so
    // that a inline macro can be generated when the WYSIWYG is detached.
    // The WYSIWYG plugin is expecting this information in the format of a
    // urlencoded JSON string stored in the data-file_info attribute of the
    // element.
    $element = media_wysiwyg_get_file_without_label($file, $tag_info['view_mode'], $settings);
    $data = drupal_json_encode(array(
      'type' => 'media',
      'fid' => $file->fid,
      'view_mode' => $tag_info['view_mode'],
      'link_text' => $tag_info['link_text'],
    ));
    $element['#attributes']['data-file_info'] = urlencode($data);
    $element['#attributes']['class'][] = 'media-element';
  }
  else {
    // Display the field elements.
    $element = array();
    $element['content']['file'] = media_wysiwyg_get_file_without_label($file, $tag_info['view_mode'], $settings);
    // Overwrite or set the file #alt attribute if it has been set in this
    // instance.
    if (!empty($element['content']['file']['#attributes']['alt'])) {
      $element['content']['file']['#alt'] = $element['content']['file']['#attributes']['alt'];
    }
    // Overwrite or set the file #title attribute if it has been set in this
    // instance.
    if (!empty($element['content']['file']['#attributes']['title'])) {
      $element['content']['file']['#title'] = $element['content']['file']['#attributes']['title'];
    }
    field_attach_prepare_view('file', array($file->fid => $file), $tag_info['view_mode']);
    entity_prepare_view('file', array($file->fid => $file));
    $element['content'] += field_attach_view('file', $file, $tag_info['view_mode']);
    if (count(element_children($element['content'])) > 1) {
      // Add surrounding divs to group them together.
      // We dont want divs when there are no additional fields to allow files
      // to display inline with text, without breaking p tags.
      $element['content']['#type'] = 'container';
      $element['content']['#attributes']['class'] = array(
        'media',
        'media-element-container',
        'media-' . $element['content']['file']['#view_mode']
      );
    }
  }
  drupal_alter('media_wysiwyg_token_to_markup', $element, $tag_info, $settings);
  return drupal_render($element);
}



/**
 * Parse the field array from the collapsed AJAX string.
 */
function media_wysiwyg_filter_field_parser($tag_info) {
  $fields = array();
  if (isset($tag_info['fields'])) {
    foreach($tag_info['fields'] as $field_name => $field_value) {
      if (strpos($field_name, 'field_') === 0) {
        $parsed_field = explode('[', str_replace(']', '', $field_name));
        if(isset($parsed_field[2])) {
          if(isset($parsed_field[3])) {
            $fields[$parsed_field[0]][$parsed_field[1]][$parsed_field[2]][$parsed_field[3]] = $field_value;
          } else {
            $fields[$parsed_field[0]][$parsed_field[1]][$parsed_field[2]] = $field_value;
          }
        } else {
          $fields[$parsed_field[0]][$parsed_field[1]] = $field_value;
        }
      }
    }
  }
  return $fields;
}
/**
 * Builds a map of media tags in the element.
 *
 * Builds a map of the media tags in an element that are being rendered to their
 * rendered HTML. The map is stored in JS, so we can transform them when the
 * editor is being displayed.
 */
function media_wysiwyg_pre_render_text_format($element) {
  // filter_process_format() copies properties to the expanded 'value' child
  // element.
  if (!isset($element['format'])) {
    return $element;
  }

  $field = &$element['value'];
  $settings = array(
    'field' => $field['#id'],
  );

  $tagmap = _media_wysiwyg_generate_tagMap($field['#value']);

  if (isset($tagmap)) {
    drupal_add_js(array('tagmap' => $tagmap), 'setting');
  }
  return $element;
}

/**
 * Creates map of inline media tags.
 *
 * Generates an array of [inline tags] => <html> to be used in filter
 * replacement and to add the mapping to JS.
 *
 * @param string $text
 *   The String containing text and html markup of textarea
 *
 * @return array
 *   An associative array with tag code as key and html markup as the value.
 *
 * @see media_process_form()
 * @see media_token_to_markup()
 */
function _media_wysiwyg_generate_tagMap($text) {
  // Making $tagmap static as this function is called many times and
  // adds duplicate markup for each tag code in Drupal.settings JS,
  // so in media_process_form it adds something like tagCode:<markup>,
  // <markup> and when we replace in attach see two duplicate images
  // for one tagCode. Making static would make function remember value
  // between function calls. Since media_process_form is multiple times
  // with same form, this function is also called multiple times.
  static $tagmap = array();
  preg_match_all("/\[\[.*?\]\]/s", $text, $matches, PREG_SET_ORDER);
  foreach ($matches as $match) {
    // We see if tagContent is already in $tagMap, if not we add it
    // to $tagmap.  If we return an empty array, we break embeddings of the same
    // media multiple times.
    if (empty($tagmap[$match[0]])) {
      // @TODO: Total HACK, but better than nothing.
      // We should find a better way of cleaning this up.
      if ($markup_for_media = media_wysiwyg_token_to_markup($match, TRUE)) {
        $tagmap[$match[0]] = $markup_for_media;
      }
      else {
        $missing = file_create_url(drupal_get_path('module', 'media') . '/images/icons/default/image-x-generic.png');
        $tagmap[$match[0]] = '<div><img src="' . $missing . '" width="100px" height="100px"/></div>';
      }
    }
  }
  return $tagmap;
}

/**
 * Return a list of view modes allowed for a file embedded in the WYSIWYG.
 *
 * @param object $file
 *   A file entity.
 *
 * @return array
 *   An array of view modes that can be used on the file when embedded in the
 *   WYSIWYG.
 */
function media_wysiwyg_get_wysiwyg_allowed_view_modes($file) {
  $enabled_view_modes = &drupal_static(__FUNCTION__, array());

  // @todo Add more caching for this.
  if (!isset($enabled_view_modes[$file->type])) {
    $enabled_view_modes[$file->type] = array();

    // Add the default view mode by default.
    $enabled_view_modes[$file->type]['default'] = array('label' => t('Default'), 'custom settings' => TRUE);

    $entity_info = entity_get_info('file');
    $view_mode_settings = field_view_mode_settings('file', $file->type);
    foreach ($entity_info['view modes'] as $view_mode => $view_mode_info) {
      // Do not show view modes that don't have their own settings and will
      // only fall back to the default view mode.
      if (empty($view_mode_settings[$view_mode]['custom_settings'])) {
        continue;
      }

      // Don't present the user with an option to choose a view mode in which
      // the file is hidden.
      $extra_fields = field_extra_fields_get_display('file', $file->type, $view_mode);
      if (empty($extra_fields['file']['visible'])) {
        continue;
      }

      // Add the view mode to the list of enabled view modes.
      $enabled_view_modes[$file->type][$view_mode] = $view_mode_info;
    }
  }

  $view_modes = $enabled_view_modes[$file->type];
  drupal_alter('media_wysiwyg_wysiwyg_allowed_view_modes', $view_modes, $file);
  return $view_modes;
}

/**
 * Form callback used when embedding media.
 *
 * Allows the user to pick a format for their media file.
 * Can also have additional params depending on the media type.
 */
function media_wysiwyg_format_form($form, $form_state, $file) {
  $form = array();
  $form['#media'] = $file;

  // Allow for overrides to the fields.
  $query_fields = isset($_GET['fields']) ? drupal_json_decode($_GET['fields']) : array();
  $fields = media_wysiwyg_filter_field_parser(array('fields' => $query_fields), $file);

  $view_modes = media_wysiwyg_get_wysiwyg_allowed_view_modes($file);
  $formats = $options = array();
  foreach ($view_modes as $view_mode => $view_mode_info) {
    // @TODO: Display more verbose information about which formatter and what it
    // does.
    $options[$view_mode] = $view_mode_info['label'];
    $element = media_wysiwyg_get_file_without_label($file, $view_mode, array('wysiwyg' => TRUE));

    // Make a pretty name out of this.
    $formats[$view_mode] = drupal_render($element);
  }

  // Add the previews back into the form array so they can be altered.
  $form['#formats'] = &$formats;

  if (!count($formats)) {
    throw new Exception('Unable to continue, no available formats for displaying media.');
    return;
  }

  // Allow for overrides to the display format.
  $default_view_mode = is_array($query_fields) && isset($query_fields['format']) ? $query_fields['format'] : variable_get('media__wysiwyg_default_view_mode', 'full');
  if (!isset($formats[$default_view_mode])) {
    $default_view_mode = key($formats);
  }

  // Add the previews by reference so that they can easily be altered by
  // changing $form['#formats'].
  $settings['media']['formatFormFormats'] = &$formats;
  $form['#attached']['js'][] = array('data' => $settings, 'type' => 'setting');

  // Add the required libraries, JavaScript and CSS for the form.
  $form['#attached']['library'][] = array('media', 'media_base');
  $form['#attached']['library'][] = array('system', 'form');
  $form['#attached']['css'][] = drupal_get_path('module', 'media_wysiwyg') . '/css/media_wysiwyg.css';
  $form['#attached']['js'][] = drupal_get_path('module', 'media_wysiwyg') . '/js/media_wysiwyg.format_form.js';

  $form['title'] = array(
    '#markup' => t('Embedding %filename', array('%filename' => $file->filename)),
  );

  $preview = media_get_thumbnail_preview($file);

  $form['preview'] = array(
    '#type' => 'markup',
    '#title' => check_plain(basename($file->uri)),
    '#markup' => drupal_render($preview),
  );

  // These will get passed on to WYSIWYG.
  $form['options'] = array(
    '#type' => 'fieldset',
    '#title' => t('options'),
  );

  $form['options']['format'] = array(
    '#type' => 'select',
    '#title' => t('Display as'),
    '#options' => $options,
    '#default_value' => $default_view_mode,
    '#description' => t('Choose the type of display you would like for this
      file. Please be aware that files may display differently than they do when
      they are inserted into an editor.')
  );

  // Add fields from the file, so that we can override them if neccesary.
  $form['options']['fields'] = array();
  foreach ($fields as $field_name => $field_value) {
    $file->{$field_name} = $field_value;
  }
  field_attach_form('file', $file, $form['options']['fields'], $form_state);
  $instance = field_info_instances('file', $file->type);
  foreach ($instance as $field_name => $field_value) {
    if (isset($instance[$field_name]['settings']) && isset($instance[$field_name]['settings']['wysiwyg_override']) && !$instance[$field_name]['settings']['wysiwyg_override']) {
      unset($form['options']['fields'][$field_name]);
    }
  }

  // Similar to a form_alter, but we want this to run first so that
  // media.types.inc can add the fields specific to a given type (like alt tags
  // on media). If implemented as an alter, this might not happen, making other
  // alters not be able to work on those fields.
  // @todo: We need to pass in existing values for those attributes.
  drupal_alter('media_wysiwyg_format_form_prepare', $form, $form_state, $file);

  if (!element_children($form['options'])) {
    $form['options']['#attributes'] = array('style' => 'display:none');
  }

  return $form;
}
