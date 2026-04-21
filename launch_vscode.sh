#!/bin/bash

EXT_FILE=".vscode/extensions.json"
PROFILE_NAME="fast-photone"

if [ ! -f "$EXT_FILE" ]; then
    echo "Error: $EXT_FILE not found!"
    exit 1
fi

echo "Checking profile: $PROFILE_NAME..."

installed_extensions=$(code --profile "$PROFILE_NAME" --list-extensions | tr '[:upper:]' '[:lower:]')

echo "Processing extensions for profile '$PROFILE_NAME'..."

ids=$(grep -oP '(?<=")[^"]+\.[^"]+(?=")' "$EXT_FILE")

for id in $ids; do
    id_lower=$(echo "$id" | tr '[:upper:]' '[:lower:]')
    
    if echo "$installed_extensions" | grep -qx "$id_lower"; then
        echo ">>> $id is already installed. Skipping..."
    else
        echo "Installing $id..."
        code --profile "$PROFILE_NAME" --install-extension "$id" --force
    fi
done

echo "Starting VS Code with profile '$PROFILE_NAME'..."
code --profile "$PROFILE_NAME" .