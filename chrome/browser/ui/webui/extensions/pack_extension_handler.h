// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_EXTENSIONS_PACK_EXTENSION_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_EXTENSIONS_PACK_EXTENSION_HANDLER_H_
#pragma once

#include <string>

#include "chrome/browser/browsing_data_remover.h"
#include "chrome/browser/extensions/pack_extension_job.h"
#include "chrome/browser/plugin_data_remover_helper.h"
#include "chrome/browser/ui/select_file_dialog.h"
#include "content/public/browser/web_ui_message_handler.h"

// Clear browser data handler page UI handler.
class PackExtensionHandler : public content::WebUIMessageHandler,
                             public SelectFileDialog::Listener,
                             public PackExtensionJob::Client {
 public:
  PackExtensionHandler();
  virtual ~PackExtensionHandler();

  void GetLocalizedValues(DictionaryValue* localized_strings);

  // WebUIMessageHandler implementation.
  virtual void RegisterMessages() OVERRIDE;

  // ExtensionPackJob::Client implementation.
  virtual void OnPackSuccess(const FilePath& crx_file,
                             const FilePath& key_file) OVERRIDE;

  virtual void OnPackFailure(const std::string& error,
                             ExtensionCreator::ErrorType) OVERRIDE;

 private:
  // SelectFileDialog::Listener implementation.
  virtual void FileSelected(const FilePath& path,
                            int index, void* params) OVERRIDE;
  virtual void MultiFilesSelected(
      const std::vector<FilePath>& files, void* params) OVERRIDE;
  virtual void FileSelectionCanceled(void* params) OVERRIDE {}

  // JavaScript callback to start packing an extension.
  void HandlePackMessage(const ListValue* args);

  // JavaScript callback to show a file browse dialog.
  // |args[0]| must be a string that specifies the file dialog type: file or
  // folder.
  // |args[1]| must be a string that specifies the operation to perform: load
  // or pem.
  void HandleSelectFilePathMessage(const ListValue* args);

  // A function to ask the page to show an alert.
  void ShowAlert(const std::string& message);

  // Used to package the extension.
  scoped_refptr<PackExtensionJob> pack_job_;

  // Returned by the SelectFileDialog machinery. Used to initiate the selection
  // dialog.
  scoped_refptr<SelectFileDialog> load_extension_dialog_;

  // Path to root directory of extension
  std::string extension_path_;

  // Path to private key file, or null if none specified
  std::string private_key_path_;

  DISALLOW_COPY_AND_ASSIGN(PackExtensionHandler);
};

#endif  // CHROME_BROWSER_UI_WEBUI_EXTENSIONS_PACK_EXTENSION_HANDLER_H_
