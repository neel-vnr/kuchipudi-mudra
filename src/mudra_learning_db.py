#!/usr/bin/env python3
"""
src/mudra_learning_db.py - Local Learning Knowledge Database Loader for MudraGuru.
"""

from __future__ import annotations

import json
import logging
from pathlib import Path
from typing import Any, Dict, List, Optional

logger = logging.getLogger("MudraLearningDB")


class MudraLearningEntry:
    """Represents learning content for a single mudra."""

    def __init__(self, data: Dict[str, Any]) -> None:
        self.mudra_id = data.get("mudra_id", "")
        self.traditional_name = data.get("traditional_name", self.mudra_id)
        self.alternate_name = data.get("alternate_name", "")
        self.meaning = data.get("meaning", "Awaiting instructor content")
        self.description = data.get("description", "Awaiting instructor content")
        self.formation_instructions = data.get("formation_instructions", "Awaiting instructor content")
        self.ordered_instruction_steps = data.get("ordered_instruction_steps", [])
        self.voice_script = data.get("voice_script", f"Lesson on {self.traditional_name}.")
        self.reference_image_path = data.get("reference_image_path", f"data/kuchipudi_references/images/{self.mudra_id}_reference.jpg")
        self.tutorial_mp4_path = data.get("tutorial_mp4_path", f"data/mudra_learning/videos/{self.mudra_id}_tutorial.mp4")
        self.content_source = data.get("content_source", "Classical Natya Shastra / Abhinaya Darpana")
        self.instructor_validation_status = data.get("instructor_validation_status", "pending_instructor_validation")

    def to_dict(self) -> Dict[str, Any]:
        return {
            "mudra_id": self.mudra_id,
            "traditional_name": self.traditional_name,
            "alternate_name": self.alternate_name,
            "meaning": self.meaning,
            "description": self.description,
            "formation_instructions": self.formation_instructions,
            "ordered_instruction_steps": self.ordered_instruction_steps,
            "voice_script": self.voice_script,
            "reference_image_path": self.reference_image_path,
            "tutorial_mp4_path": self.tutorial_mp4_path,
            "content_source": self.content_source,
            "instructor_validation_status": self.instructor_validation_status,
        }


class MudraLearningDatabase:
    """Manages loading and querying of the 30-mudra learning content knowledge base."""

    def __init__(self, db_path: Path = Path("data/mudra_learning/learning_content.json")) -> None:
        self.db_path = db_path
        self._entries: Dict[str, MudraLearningEntry] = {}
        self._order: List[str] = []
        self.load_database()

    def load_database(self) -> None:
        if self.db_path.is_file():
            try:
                with open(self.db_path, "r", encoding="utf-8") as f:
                    data = json.load(f)
                    entries_dict = data.get("mudra_entries", {})
                    # Natural numerical sort key
                    import re
                    def natural_sort_key(s: str) -> List[Any]:
                        return [int(text) if text.isdigit() else text.lower() for text in re.split(r"(\d+)", s)]

                    self._order = sorted(list(entries_dict.keys()), key=natural_sort_key)
                    for k in self._order:
                        self._entries[k] = MudraLearningEntry(entries_dict[k])
                logger.info(f"Loaded learning database with {len(self._entries)} mudra entries.")
            except Exception as e:
                logger.error(f"Failed to load learning database: {e}")
        else:
            logger.warning(f"Learning database not found at {self.db_path}")

    def get_all_mudra_ids(self) -> List[str]:
        return self._order

    def get_entry(self, mudra_id: str) -> Optional[MudraLearningEntry]:
        # Direct or fuzzy match
        if mudra_id in self._entries:
            return self._entries[mudra_id]
        for k, entry in self._entries.items():
            if k.lower() == mudra_id.lower() or k.split("-")[-1].lower() == mudra_id.split("-")[-1].lower():
                return entry
        return None

    def get_entry_by_index(self, idx: int) -> Optional[MudraLearningEntry]:
        if not self._order:
            return None
        safe_idx = idx % len(self._order)
        return self._entries.get(self._order[safe_idx])
