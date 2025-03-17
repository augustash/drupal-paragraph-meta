<?php

/**
 * @file
 * Builds placeholder replacement tokens for paragraph related data.
 */

use Drupal\Core\Entity\ContentEntityInterface;
use Drupal\Core\Field\FieldItemListInterface;
use Drupal\Core\Render\BubbleableMetadata;

/**
 * Impements hook_token_info_alter().
 */
function paragraph_meta_token_info_alter(&$data) {
  $data['tokens']['node']['paragraph_meta_smart_title'] = [
    'name' => t('Paragraph Meta: Smart Title'),
    'description' => t('Will determine the best title to use for representing this entity. Example: [node:paragraph_meta_smart_title:value]'),
    'type' => 'paragraph_meta',
  ];
  $data['tokens']['node']['paragraph_meta_smart_description'] = [
    'name' => t('Paragraph Meta: Smart Description'),
    'description' => t('Will determine the best description to use for representing this entity. Example: [node:paragraph_meta_smart_description:value]'),
    'type' => 'paragraph_meta',
  ];
  $data['tokens']['node']['paragraph_meta_smart_image'] = [
    'name' => t('Paragraph Meta: Smart Image URL'),
    'description' => t('Will determine the best image to use for representing this entity. Example: [node:paragraph_meta_smart_image:meta:url]'),
    'type' => 'paragraph_meta',
  ];
}

/**
 * Implements hook_tokens().
 */
function paragraph_meta_tokens($type, $tokens, array $data, array $options, BubbleableMetadata $bubbleable_metadata) {
  $token_service = \Drupal::token();
  $replacements = [];
  if ($type == 'node' && !empty($data['node'])) {
    /** @var \Drupal\node\NodeInterface $node */
    $node = $data['node'];
    $lookup = [
      // Smart title.
      'paragraph_meta_smart_title' => [
        'field_type' => 'text',
        'base_fields' => [
          'field_metatags:title',
          'field_meta:title',
        ],
        'fallback' => 'paragraph_meta_smart_title_fallback',
      ],
      // Smart image.
      'paragraph_meta_smart_image' => [
        'field_type' => 'image',
        'base_fields' => [
          'field_banner',
          'field_featured_image',
          'field_image',
          'field_images',
          'field_file_image',
          'field_body',
        ],
        'fallback' => 'paragraph_meta_smart_image_fallback',
      ],
      // Smart description.
      'paragraph_meta_smart_description' => [
        'field_type' => 'text_long',
        'base_fields' => [
          'field_metatags:description',
          'field_meta:description',
          'field_description',
          'field_teaser',
          'field_body',
        ],
        'truncate' => 160,
        'fallback' => 'paragraph_meta_smart_description_fallback',
      ],
    ];
    foreach ($lookup as $prefix => $info) {
      if ($found_tokens = $token_service->findWithPrefix($tokens, $prefix)) {
        $smart_data = paragraph_meta_smart_data($node, $info['field_type'], $info['base_fields']);
        if ($smart_data) {
          if (!empty($smart_data['value'])) {
            $smart_replacements[$smart_data['fieldname']] = $smart_data['value'];
          }
          else {
            $entity_type = $smart_data['entity']->getEntityTypeId();
            $smart_tokens = [];
            foreach ($found_tokens as $key => $value) {
              $fieldname = $smart_data['fieldname'];
              if (!empty($smart_data['token'])) {
                $fieldname .= ':' . $smart_data['token'];
              }
              $smart_tokens[$smart_data['fieldname'] . ':' . $key] = '[' . $entity_type . ':' . $fieldname . ':' . $key . ']';
            }
            $smart_replacements = $token_service->generate($entity_type, $smart_tokens, [$entity_type => $smart_data['entity']], $options, $bubbleable_metadata);
          }
          if (count($found_tokens) === count($smart_replacements)) {
            foreach ($smart_replacements as &$replacement) {
              if (!empty($info['truncate']) && is_string($replacement)) {
                $replacement = str_replace(
                      '  ', ' ', str_replace(
                          '  ', ' ', str_replace(
                              [
                                "\r",
                                "\n",
                              ], ' ', strip_tags($replacement)
                          )
                      )
                  );
                $replacement = strlen($replacement) > $info['truncate'] ? substr($replacement, 0, $info['truncate']) . "..." : $replacement;
              }
            }
            $replacements += array_combine($found_tokens, $smart_replacements);
          }
        }
        elseif (!empty($info['fallback'])) {
          $replacements = $info['fallback']($replacements, $found_tokens, $data);
        }
      }
    }
  }
  return $replacements;
}

/**
 * Smart description fallback.
 */
function paragraph_meta_smart_title_fallback($replacements, array $found_tokens, array $data) {
  foreach ($found_tokens as $key => $value) {
    if (!empty($data['node'])) {
      if (\Drupal::service('path.matcher')->isFrontPage()) {
        $replacements[$value] = \Drupal::config('system.site')->get('name') . ' | ' . $data['node']->label();
      }
      else {
        $replacements[$value] = $data['node']->label() . ' | ' . \Drupal::config('system.site')->get('name');
      }
    }
  }
  return $replacements;
}

/**
 * Smart image fallback.
 */
function paragraph_meta_smart_image_fallback($replacements, array $found_tokens, array $data) {
  // Handle fallbacks.
  // @todo Open this open to other modules.
  if (\Drupal::moduleHandler()->moduleExists('real_favicon')) {
    $favicon = real_favicon_load_by_theme();
    if ($favicon) {
      foreach ($found_tokens as $key => $value) {
        $replacements[$value] = \Drupal::service('file_url_generator')->generateAbsoluteString($favicon->getManifestLargeImage());
      }
    }
  }
  return $replacements;
}

/**
 * Smart description fallback.
 */
function paragraph_meta_smart_description_fallback($replacements, array $found_tokens, array $data) {
  foreach ($found_tokens as $key => $value) {
    $replacements[$value] = \Drupal::config('system.site')->get('slogan');
  }
  return $replacements;
}

/**
 * Given an entity, find the best image file entity.
 *
 * @param \Drupal\Core\Entity\ContentEntityInterface $entity
 *   The entity.
 */
function paragraph_meta_smart_data(ContentEntityInterface $entity, $field_type, $base_fields = []) {
  $cache = &drupal_static(__FUNCTION__, []);
  $key = implode('_', [
    $entity->getEntityTypeId(),
    $entity->id(),
    $field_type,
    implode('_', $base_fields),
  ]);
  if (!isset($cache[$key])) {
    $data = [];
    foreach ($base_fields as $fieldname) {
      $parts = explode(':', $fieldname, 2);
      $fieldname = $parts[0];
      $path = isset($parts[1]) ? explode(':', $parts[1]) : [];

      if ($entity->hasField($fieldname)) {
        $base_data = paragraph_meta_smart_find($entity->get($fieldname), $field_type, $path);
        if ($base_data) {
          $data = $base_data;
          break;
        }
      }
    }
    if (empty($data)) {
      foreach ($entity->getFields() as $field) {
        if ($field->getFieldDefinition()->getType() === 'entity_reference_revisions') {
          $paragraph_data = paragraph_meta_smart_find($field, $field_type);
          if ($paragraph_data) {
            $data = $paragraph_data;
            break;
          }
        }
      }
    }
    $cache[$key] = $data;
  }
  return $cache[$key];
}

/**
 * Given a field, locate an image field.
 */
function paragraph_meta_smart_find(FieldItemListInterface $field, $field_type, $path = []) {
  if (!$field->isEmpty()) {
    switch ($field->getFieldDefinition()->getType()) {
      case 'text_long':
        if ($field_type === 'text_long') {
          return [
            'fieldname' => $field->getName(),
            'entity' => $field->getEntity(),
            'path' => $path,
            'value' => $field->first()->value,
          ];
        }
        break;

      case 'entity_reference_revisions':
      case 'entity_reference':
        foreach ($field as $item) {
          $entity = $item->entity;
          if ($entity && $entity instanceof ContentEntityInterface) {
            // Check all fields of the referenced entity (including paragraphs).
            foreach ($entity->getFields() as $subfield) {
              if ($subfield->getFieldDefinition()->getType() === $field_type) {
                return paragraph_meta_smart_find($subfield, $field_type, $path);
              }
            }
          }
        }
        break;
    }
  }
  return NULL;
}
